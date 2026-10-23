// SPDX-License-Identifier: GPL-3.0-or-later
// Hardware mapping and video layout ported from omikron88/jondra (GPLv3).
#include <ondra/machine.hpp>
#include <ondra/files.hpp>
#include <ondra/resources.hpp>
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace ondra {
Machine::Machine() : cpu_(), romType_(Rom::Basic), romName_("basic"),
    pixels_(Width * Height), cycles_(0), frameStart_(0), multiplier_(1),
    resolution_(255), frameTarget_(0), interruptEnd_(0), cycleDebt_(0),
    a0_(0), a1_(0), a3_(0), frameActive_(false) {
    cpu_.context = this;
    cpu_.fetch_opcode = cpu_.fetch = cpu_.read = cpu_.nop = &Machine::cpuRead;
    cpu_.write = &Machine::cpuWrite;
    cpu_.in = &Machine::cpuIn; cpu_.out = &Machine::cpuOut;
    cpu_.inta = &Machine::interruptAck;
    cpu_.options = Z80_MODEL_ZILOG_NMOS;
    custom_.fill(255);
    z80_power(&cpu_, 1);
    installRom(romType_);
    reset();
}
void Machine::installRom(Rom type) {
    if (type == Rom::Custom) { rom_ = custom_; return; }
    std::string prefix;
    switch (type) {
    case Rom::Basic: prefix = "Ondra_BASICEXP_V5"; break;
    case Rom::Tesla: prefix = "Ondra_TESLA_V5"; break;
    case Rom::Vili: prefix = romName_ == "vili25" ? "Ondra_ViLi_v25" : "Ondra_ViLi_v27"; break;
    case Rom::Plus: prefix = "Ondra_PLUS"; break;
    default: throw std::runtime_error("Invalid ROM type");
    }
    for (unsigned bank = 0; bank < 2; ++bank) {
        const std::vector<std::uint8_t> data = resource(prefix + (bank ? "_b.rom" : "_a.rom"));
        for (unsigned i = 0; i < 8192; ++i) rom_[bank * 8192 + i] = data[i % data.size()];
    }
}
void Machine::selectRom(const std::string& name) {
    Rom type;
    if (name == "basic") type = Rom::Basic;
    else if (name == "tesla") type = Rom::Tesla;
    else if (name == "vili25" || name == "vili27" || name == "vili") type = Rom::Vili;
    else if (name == "plus") type = Rom::Plus;
    else throw std::runtime_error("Unknown ROM: " + name);
    romName_ = name; romType_ = type; installRom(type); reset();
}
std::string Machine::romName() const {
    switch (romType_) {
    case Rom::Basic: return "basic";
    case Rom::Tesla: return "tesla";
    case Rom::Vili: return romName_ == "vili25" ? "vili25" : "vili27";
    case Rom::Plus: return "plus";
    default: return "custom";
    }
}
void Machine::customRom(const std::string& a, const std::string& b) {
    const auto first = readFile(a, 8192), second = readFile(b, 8192);
    for (const auto* data : {&first, &second})
        if (data->size() != 2048 && data->size() != 4096 && data->size() != 8192)
            throw std::runtime_error("Each ROM bank must contain 2048, 4096 or 8192 bytes");
    for (unsigned i = 0; i < 8192; ++i) {
        custom_[i] = first[i % first.size()]; custom_[i + 8192] = second[i % second.size()];
    }
    romType_ = Rom::Custom; installRom(romType_); reset();
}
void Machine::reset(bool clearRam) {
    if (clearRam) for (unsigned i = 0; i < ram_.size(); ++i) ram_[i] = ((i / 128) & 1) ? 255 : 0;
    z80_instant_reset(&cpu_);
    releaseKeys();
    a0_ = a1_ = a3_ = 0; cycles_ = frameStart_ = 0;
    resolution_ = 255; cycleDebt_ = 0; frameActive_ = false;
    tape_.setMotor(false); sound_.reset(); sd_.reset(0); printer_.reset(0); bt100_.reset(0);
}
void Machine::setTurbo(bool enabled) {
    const unsigned next=enabled?2:1;
    printer_.changeSpeed(cycles_,multiplier_,next); bt100_.changeSpeed(cycles_,multiplier_,next);
    multiplier_=next;
}
zuint8 Machine::cpuRead(void* p, zuint16 a) { return static_cast<Machine*>(p)->read(a); }
void Machine::cpuWrite(void* p, zuint16 a, zuint8 v) { static_cast<Machine*>(p)->write(a, v); }
zuint8 Machine::cpuIn(void* p, zuint16 a) { return static_cast<Machine*>(p)->input(a); }
void Machine::cpuOut(void* p, zuint16 a, zuint8 v) { static_cast<Machine*>(p)->output(a, v); }
std::uint8_t Machine::read(std::uint16_t a) {
    if (a < 0x4000 && !(a3_ & 2)) return rom_[a];
    if (a >= 0xe000 && (a3_ & 4)) {
        // Only A[3:0] is decoded: every row repeats throughout E000-FFFF.
        const unsigned row=a&15;
        std::uint8_t v = keys_[row];
        if (tape_.loaded()) v = static_cast<std::uint8_t>((v & 127) | (tape_.level() ? 128 : 0));
        if (sound_.melodik() && row == 15) v &= ~32;
        if (sd_.enabled()) v = static_cast<std::uint8_t>((v & ~64) | (sd_.rxLevel(cycles_) ? 64 : 0));
        if (bt100_.enabled()) {
            if (row==9) v=static_cast<std::uint8_t>((v&0xf0)|bt100_.sensorLines(cycles_,multiplier_));
            else if (row<9) v=static_cast<std::uint8_t>(v&(0xf0|bt100_.sensorLines(cycles_,multiplier_)));
        }
        if (printer_.enabled()) v=static_cast<std::uint8_t>((v&~32)|(printer_.busy(cycles_)?32:0));
        return v;
    }
    return ram_[a];
}
bool Machine::write(std::uint16_t a, std::uint8_t v) {
    if ((a < 0x4000 && !(a3_ & 2)) || (a >= 0xe000 && (a3_ & 4))) return false;
    ram_[a] = v; return true;
}
std::uint8_t Machine::input(std::uint16_t port) {
    if ((a3_ & 0x30) == 0) {
        const unsigned v = port & 255;
        resolution_ = ((v << 1) | (v >> 7)) & 255;
    }
    return 255;
}
void Machine::output(std::uint16_t port, std::uint8_t value) {
    sound_.advance(frameActive_ ? cycles_ - frameStart_ : 0, multiplier_);
    if (!(port & 8)) { a3_ = value; tape_.recordLevel((value & 8) != 0); }
    if (!(port & 1)) {
        if ((a0_&8) && !(value&8)) printer_.strobe(a1_,cycles_,multiplier_);
        a0_ = value; sd_.txWrite((value >> 2) & 1, cycles_);
        sound_.speaker(value >> 5); tape_.setMotor((value & 16) != 0);
    }
    if (!(port & 2)) {
        a1_ = value;
        bt100_.write(value,cycles_,multiplier_);
        if ((a0_ & 8) && sound_.melodik() && !printer_.enabled() && !bt100_.enabled()) sound_.psgWrite(value);
    }
}
void Machine::key(unsigned row, unsigned bit, bool down) {
    if (row >= 10 || bit >= 5) throw std::runtime_error("Invalid keyboard matrix position");
    if (down) keys_[row] &= static_cast<std::uint8_t>(~(1u << bit));
    else keys_[row] |= static_cast<std::uint8_t>(1u << bit);
}
unsigned Machine::step() {
    unsigned consumed = 0;
    do {
        const unsigned n = static_cast<unsigned>(z80_run(&cpu_, 1));
        if (!n) throw std::runtime_error("Z80 core made no progress");
        cycles_ += n; consumed += n; tape_.advance(n, multiplier_);
        // The 8253 pulse expires even when stepping a paused frame or when
        // interrupts are disabled; accepting an interrupt does not end it.
        if (frameActive_ && cpu_.int_line && cycles_-frameStart_>=interruptEnd_) z80_int(&cpu_,0);
        // Complete prefixes as one debugger step. Bound malformed streams of
        // DD/FD prefixes so that the desktop stays responsive.
    } while ((cpu_.resume == Z80_RESUME_XY || cpu_.resume == Z80_RESUME_IM0_XY) && consumed < FrameCycles);
    return consumed;
}
void Machine::jump(std::uint16_t address) {
    Z80_PC(cpu_) = address;
    cpu_.resume = 0; cpu_.halt_line = 0; cpu_.data.uint8_array[0] = 0;
}
unsigned Machine::frameBudget() const {
    return (videoEnabled() ? (312 - resolution_) * 128 : FrameCycles) * multiplier_;
}
bool Machine::runFrame() {
    if (!frameActive_) {
        frameActive_ = true; frameStart_ = cycles_;
        sd_.frameStart(cycles_, multiplier_); sound_.beginFrame();
        interruptEnd_ = 128*multiplier_; // 64 us at either CPU clock.
        frameTarget_ = frameBudget();
        z80_int(&cpu_, 1);
    }
    while (cycles_ - frameStart_ + cycleDebt_ < frameTarget_) {
        if (!breakpoints_.empty() && breakpoints_.count(Z80_PC(cpu_))) return false;
        step();
    }
    z80_int(&cpu_, 0);
    cycleDebt_ = static_cast<unsigned>(cycles_ - frameStart_ + cycleDebt_ - frameTarget_);
    sound_.finishFrame(); bt100_.advance(cycles_,multiplier_); frameActive_ = false;
    return true;
}
const std::vector<std::uint32_t>& Machine::pixels() {
    std::fill(pixels_.begin(), pixels_.end(), 0xff000000u);
    if (!videoEnabled()) return pixels_;
    unsigned out = 0;
    for (unsigned y = resolution_; y > 0; --y) {
        for (unsigned x = 0xff00; x >= 0xd800; x -= 0x100) {
            const unsigned address = x | (y >> 1) | ((y & 1) << 7);
            const unsigned bits = ram_[address];
            for (unsigned bit = 0; bit < 8; ++bit) pixels_[out++] = (bits & (128 >> bit)) ? 0xffffffffu : 0xff000000u;
        }
    }
    return pixels_;
}
std::uint32_t Machine::screenHash() {
    std::uint32_t h = 2166136261u;
    for (std::uint32_t pixel : pixels()) { h ^= pixel & 255; h *= 16777619u; }
    return h;
}
void Machine::screenshot(const std::string& path) {
    const std::string header = "P6\n320 256\n255\n";
    std::vector<std::uint8_t> data(header.begin(), header.end());
    for (std::uint32_t p : pixels()) for (int shift = 16; shift >= 0; shift -= 8) data.push_back((p >> shift) & 255);
    writeFile(path, data);
}
void Machine::loadRaw(const std::string& path, unsigned address) {
    const auto data = readFile(path, 65536);
    if (address >= 65536 || data.size() > 65536 - address) throw std::runtime_error("Binary exceeds RAM");
    std::copy(data.begin(), data.end(), ram_.begin() + address);
}
void Machine::loadBinary(const std::string& path, bool autorun) {
    const auto data = readFile(path, 1024 * 1024);
    std::array<std::uint8_t, 65536> staged = ram_;
    std::size_t p = 0; int entry = -1;
    while (p < data.size()) {
        const unsigned type = data[p++];
        const unsigned address = word(data, p); p += 2;
        if (type == 2) { entry = address; break; }
        if (type != 1) throw std::runtime_error("Invalid OndraSD BIN block type");
        const unsigned size = word(data, p); p += 2;
        if (size > 65536 - address || size > data.size() - p) throw std::runtime_error("Invalid BIN block length");
        std::copy(data.begin() + p, data.begin() + p + size, staged.begin() + address); p += size;
    }
    if (data.empty() || p != data.size()) throw std::runtime_error("Empty or trailing BIN data");
    ram_ = staged;
    if (autorun && entry >= 0) jump(static_cast<std::uint16_t>(entry));
}
void Machine::saveBinary(const std::string& path, unsigned address, unsigned size, int entry) const {
    if (address >= 65536 || !size || size > 65536 - address || entry > 65535 || entry < -1)
        throw std::runtime_error("Invalid BIN range");
    std::vector<std::uint8_t> data;
    while (size) {
        const unsigned count = std::min(size, 65535u);
        data.push_back(1); putWord(data, address); putWord(data, count);
        data.insert(data.end(), ram_.begin() + address, ram_.begin() + address + count);
        size -= count; address += count;
    }
    if (entry >= 0) { data.push_back(2); putWord(data, static_cast<std::uint16_t>(entry)); }
    writeFile(path, data);
}
}
