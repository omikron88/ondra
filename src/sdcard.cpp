// SPDX-License-Identifier: GPL-3.0-or-later
// Protocol/timing ported from machine/OndraSD.java in omikron88/jondra.
#include <ondra/sdcard.hpp>
#include <ondra/files.hpp>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <limits>
#include <set>
#include <stdexcept>

namespace ondra {
namespace {
const double CpuHz = 1996800.0, Cycle8051 = 12 * CpuHz / 22118400.0;
std::string upper(std::string s) { for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); return s; }
std::string clean(const std::string& s) {
    std::string out;
    for (unsigned char c : upper(s)) {
        if (c > 32 && c < 127 && std::string("\"*+,./:;<=>?[\\]|").find(c) == std::string::npos) out += c;
        else if (c != ' ' && c != '.') out += '_';
    }
    return out;
}
std::string fatName(std::string base, std::string ext) {
    base.resize(8, ' '); ext.resize(3, ' '); return base + ext;
}
}
SdCard::Time SdCard::Tx::end() const { return start + static_cast<Time>(std::ceil(10 * bit)); }
SdCard::SdCard() : frameClock_(0), frameReal_(0), nextBroadcast_(0), free_(0), lastEnd_(0), rxStart_(0), pendingTime_(0),
    multiplier_(1), baud_(9600), delay_(0xc0), txLine_(0), rxBit_(0), rxShift_(0), command_(-1), pending_(-1),
    frameStarted_(false), broadcast_(false), waitHigh_(false), rxActive_(false), rxBitTime_(0) {}
void SdCard::configure(const std::string& root) {
    const std::string path = root.empty() ? "" : canonical(root);
    if (!root.empty() && (path.empty() || !isDirectory(path))) throw std::runtime_error("SD directory does not exist: " + root);
    root_ = path; current_ = root_; reset(0);
}
SdCard::Time SdCard::real(std::uint64_t clock) const { return frameReal_ + (static_cast<Time>(clock) - frameClock_) / multiplier_; }
double SdCard::bitTime() const { return CpuHz / baud_; }
double SdCard::gapTime() const { return (2 * (delay_ ? delay_ : 256) + 40) * Cycle8051; }
void SdCard::reset(std::uint64_t clock) {
    frameStarted_ = false; frameClock_ = frameReal_ = static_cast<Time>(clock);
    queue_.clear(); lastEnd_ = 0; rxActive_ = false; txLine_ = 0;
    command_ = pending_ = -1; params_.clear(); waitHigh_ = false;
    restart(real(clock) + static_cast<Time>(1.1 * CpuHz));
}
void SdCard::restart(Time t) {
    baud_ = 9600; delay_ = 0xc0; current_ = root_; command_ = -1; params_.clear(); free_ = t;
    broadcast_ = enabled();
    if (enabled()) { send(13, t); nextBroadcast_ = free_; }
}
void SdCard::frameStart(std::uint64_t clock, unsigned multiplier) {
    if (!frameStarted_) { frameStarted_ = true; frameReal_ = static_cast<Time>(clock); }
    else {
        const Time ran = (static_cast<Time>(clock) - frameClock_) / multiplier_;
        const bool busy = rxActive_ || !queue_.empty() || pending_ >= 0 || command_ >= 0;
        frameReal_ += busy ? ran : std::max<Time>(39936, ran);
    }
    frameClock_ = static_cast<Time>(clock); multiplier_ = multiplier;
    if (enabled()) advance(real(clock));
}
void SdCard::txWrite(unsigned level, std::uint64_t clock) {
    if (!enabled()) return;
    const Time t = real(clock); advance(t); level &= 1;
    if (level == txLine_) return;
    if (txLine_ && !level && !rxActive_) {
        rxActive_ = true; rxStart_ = t; rxBitTime_ = bitTime(); rxBit_ = rxShift_ = 0;
    }
    txLine_ = level;
    if (level && waitHigh_) { waitHigh_ = false; restart(std::max(t, free_)); }
}
bool SdCard::rxLevel(std::uint64_t clock) {
    if (!enabled()) return true;
    const Time t = real(clock); advance(t);
    if (queue_.empty() || t < queue_.front().start) return true;
    const Tx& b = queue_.front();
    unsigned bit = static_cast<unsigned>((t - b.start) / b.bit);
    return bit == 0 ? false : bit >= 9 ? true : ((b.value >> (bit - 1)) & 1) != 0;
}
void SdCard::advance(Time t) {
    while (!queue_.empty() && queue_.front().end() <= t) queue_.pop_front();
    while (rxActive_) {
        const Time ts = rxStart_ + static_cast<Time>((rxBit_ + 0.5) * rxBitTime_);
        if (ts >= t) break;
        if (rxBit_ == 0 && txLine_) { rxActive_ = false; break; }
        if (rxBit_ > 0 && rxBit_ <= 8) rxShift_ |= txLine_ << (rxBit_ - 1);
        else if (rxBit_ == 9) {
            rxActive_ = false; received(rxShift_, ts);
            if (!txLine_) { rxActive_ = true; rxStart_ = ts; rxBitTime_ = bitTime(); rxBit_ = rxShift_ = 0; continue; }
            break;
        }
        ++rxBit_;
    }
    if (pending_ >= 0 && t >= free_) {
        const unsigned value = static_cast<unsigned>(pending_); pending_ = -1;
        process(value, std::max(free_, pendingTime_));
    }
    if (broadcast_ && t >= nextBroadcast_ && nextBroadcast_ >= free_) {
        const std::string loader = find(root_, "__LOADERBIN", false);
        if (loader.empty()) { nextBroadcast_ = std::numeric_limits<Time>::max(); return; }
        sendFile(loader, std::max(nextBroadcast_, t), false);
        nextBroadcast_ = free_ + static_cast<Time>(255 * 6 * 2048 * Cycle8051);
    }
}
void SdCard::received(unsigned value, Time t) {
    if (t < free_) { if (pending_ < 0) { pending_ = static_cast<int>(value); pendingTime_ = t; } }
    else process(value, t);
}
void SdCard::process(unsigned value, Time t) {
    if (waitHigh_) return;
    if (command_ < 0) { command_ = static_cast<int>(value); params_.clear(); }
    else params_.push_back(value);
    free_ = std::max(free_, t);
    const unsigned n = static_cast<unsigned>(params_.size());
    switch (command_) {
    case 0: case 'X': case 'H':
        command_ = -1; if (txLine_) restart(t); else waitHigh_ = true; break;
    case 'N': case 'U': case 'V': command_ = -1; broadcast_ = false; break;
    case 'L': command_ = -1; broadcast_ = true; nextBroadcast_ = free_; break;
    case 'O': case 'Q': command_ = -1; break;
    case 'A': if (n == 1) { command_ = -1; baud_ = (params_[0] & 1) ? 57600 : 9600; delay_ = (params_[0] & 1) ? 0x20 : 0xc0; } break;
    case 'I': if (n == 1) command_ = -1; break;
    case 'E': if (n == 1) { command_ = -1; send(params_[0], t); } break;
    case 'K': command_ = -1; send(0x12, t); break;
    case 'M': {
        command_ = -1;
        const std::string text = "\r\nOndraSD interface (2.2) + RTC\r\nM1 (c) 2015, 2016\r\nhttps://sites.google.com/site/ondraspo186\r\n";
        for (unsigned char c : text) send(c, t);
        break;
    }
    case 'G': case 'J': case 'P': case 'Z': command_ = -1; send('+', t + static_cast<Time>(10 * delay_ * Cycle8051)); break;
    case 'T': command_ = -1; sendRtc(t); break;
    case 'S': if (n == 7) { command_ = -1; setRtc(); } break;
    case 'B': case 'R': case 'W': if (n == 4) { command_ = -1; send(0, t + 2000); } break;
    case 'D': command_ = -1; sendDirectory(t + 2000); break;
    case 'C': case 'F':
        if (n == 11) {
            const int cmd = command_; command_ = -1;
            const std::string name(params_.begin(), params_.end());
            if (cmd == 'C') { if (name[0]!='/') search(name,t); changeDirectory(name); }
            else { const std::string file = find(current_, name, false); const Time end=search(name,t); if (!file.empty()) sendFile(file, end, name[8] == 'T'); }
        }
        break;
    default: command_ = -1; break;
    }
}
void SdCard::send(unsigned value, Time t) {
    const Time start = std::max(std::max(t, lastEnd_) + static_cast<Time>(gapTime()), free_);
    Tx b = {start, bitTime(), value & 255}; queue_.push_back(b);
    lastEnd_ = free_ = b.end();
}
void SdCard::pause(unsigned cycles) { lastEnd_ += static_cast<Time>(cycles * Cycle8051); free_ = std::max(free_, lastEnd_); }
void SdCard::sendFile(const std::string& path, Time t, bool tap) {
    const auto data = readFile(path, 16 * 1024 * 1024);
    bool skip = true; unsigned count = 26;
    for (std::size_t i = 0; i < data.size(); ++i) {
        if (i && !(i & 511)) pause(3000);
        if (!tap || !skip) send(data[i], t);
        if (tap && --count == 0) { skip = !skip; count = skip ? 27 : 1024; }
    }
    if (!tap) pause(3000);
}
SdCard::Time SdCard::search(const std::string& name,Time time) {
    unsigned count=0;
    for (const auto& entry:list(current_)) { ++count; if (entry.fat==name) break; }
    const Time end=time+4000+static_cast<Time>(count*1500.0*Cycle8051);
    free_=std::max(free_,end); return end;
}
std::vector<SdCard::Entry> SdCard::list(const std::string& path) const {
    std::vector<Entry> entries;
    if (!inside(path, root_)) return entries;
    if (path != root_) {
        entries.push_back(Entry{fatName(".", ""), path, true, 0});
        const std::string parent = canonical(path + "/..");
        entries.push_back(Entry{fatName("..", ""), inside(parent, root_) ? parent : root_, true, 0});
    }
    std::set<std::string> used;
    auto files = directory(path);
    std::sort(files.begin(), files.end(), [](const FileEntry& a, const FileEntry& b) {
        return upper(a.name) == upper(b.name) ? a.name < b.name : upper(a.name) < upper(b.name);
    });
    for (const FileEntry& f : files) {
        if (!inside(f.path, root_)) continue;
        const auto dot = f.directory ? std::string::npos : f.name.find_last_of('.');
        const std::string base = clean(f.name.substr(0, dot));
        const std::string ext = dot == std::string::npos ? "" : clean(f.name.substr(dot + 1));
        const bool exact = !base.empty() && base.size() <= 8 && ext.size() <= 3 && upper(f.name) == base + (dot == std::string::npos ? "" : "." + ext);
        std::string fat = fatName(base, ext);
        if (!exact || used.count(fat)) {
            for (unsigned n = 1; ; ++n) {
                const std::string suffix = "~" + std::to_string(n);
                fat = fatName(base.substr(0, 8 - std::min<std::size_t>(8, suffix.size())) + suffix, ext);
                if (!used.count(fat)) break;
            }
        }
        used.insert(fat); entries.push_back(Entry{fat, f.path, f.directory, f.size});
    }
    return entries;
}
std::string SdCard::find(const std::string& path, const std::string& fat, bool dir) const {
    for (const Entry& e : list(path)) if (e.dir == dir && e.fat == fat) return e.path;
    return "";
}
void SdCard::sendDirectory(Time t) {
    for (const Entry& e : list(current_)) {
        if (!e.dir && (e.size >= 65536 || (e.fat.substr(8) != "BIN" && e.fat.substr(8) != "TAP"))) continue;
        for (unsigned char c : e.fat) send(c, t);
        send(e.dir ? 0x51 : 0x71, t); send(0, t); pause(1500);
    }
    send(255, t);
    pause(3000);
}
void SdCard::changeDirectory(const std::string& name) {
    if (name[0] == '/') current_ = root_;
    else { const std::string path = find(current_, name, true); if (!path.empty()) current_ = path; }
}
void SdCard::sendRtc(Time t) {
    for (auto value:rtc_.read()) send(value,t+1000);
}
void SdCard::setRtc() {
    Ds1307::Registers values;
    for (unsigned n=0;n<values.size();++n) values[n]=static_cast<std::uint8_t>(params_[n]);
    rtc_.write(values);
}
}
