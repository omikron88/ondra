// SPDX-License-Identifier: GPL-3.0-or-later
#include <ondra/sound.hpp>
#include <ondra/resources.hpp>
#include <algorithm>
#include <string>

namespace ondra {
Psg::Psg() : latch_(0), lfsr_(0x8000), fraction_(0) {
    for (unsigned n = 0; n < 4; ++n) {
        registers_[n * 2] = 1; registers_[n * 2 + 1] = 15;
        counters_[n] = 1; polarity_[n] = true;
    }
    registers_[6] = 0;
}
void Psg::write(std::uint8_t v) {
    if (v & 0x80) {
        latch_ = (v >> 4) & 7;
        registers_[latch_] = (registers_[latch_] & 0x3f0) | (v & 15);
    } else if (latch_ < 6 && !(latch_ & 1)) {
        registers_[latch_] = (registers_[latch_] & 15) | ((v & 63) << 4);
    } else registers_[latch_] = v & 15;
    if (latch_ & 1) registers_[latch_] &= 15;
    if (latch_ == 6) { registers_[6] &= 7; lfsr_ = 0x8000; }
}
std::int16_t Psg::sample() {
    fraction_ += 4000000.0 / (16.0 * 44100.0);
    unsigned ticks = static_cast<unsigned>(fraction_);
    fraction_ -= ticks;
    while (ticks--) {
        bool tone2Rising = false;
        for (unsigned n = 0; n < 3; ++n) {
            if (--counters_[n] == 0) {
                const unsigned tone = registers_[n * 2];
                counters_[n] = std::max(1u, tone);
                // Dividers up to 6 are above hearing on real hardware (register 6 is about
                // 18.6 kHz at 4 MHz), so the flip-flop output is held instead of toggling:
                // toggling it would only alias down into the audible band as a whistle.
                if (tone > 6) {
                    polarity_[n] = !polarity_[n];
                    if (n == 2 && polarity_[n]) tone2Rising = true;
                } else {
                    polarity_[n] = true;
                }
            }
        }
        bool shift = false;
        if ((registers_[6] & 3) == 3) shift = tone2Rising;
        else if (--counters_[3] == 0) {
            counters_[3] = 16u << (registers_[6] & 3);
            polarity_[3] = !polarity_[3]; shift = polarity_[3];
        }
        if (shift) {
            const unsigned feedback = (registers_[6] & 4) ? ((lfsr_ ^ (lfsr_ >> 3)) & 1) : (lfsr_ & 1);
            lfsr_ = (lfsr_ >> 1) | (feedback << 15);
        }
    }
    static const int amplitude[16] = {1800,1430,1136,902,716,569,452,359,285,226,180,143,113,90,71,0};
    int out = 0;
    for (unsigned n = 0; n < 3; ++n) out += amplitude[registers_[n * 2 + 1]] * (polarity_[n] ? 1 : -1);
    out += amplitude[registers_[7]] * ((lfsr_ & 1) ? 1 : -1);
    return static_cast<std::int16_t>(out);
}
Sound::Sound() : tone_(0), samplePosition_(0), melodik_(false) {
    samples_[0] = std::vector<std::uint8_t>(2, 0);
    for (unsigned n = 1; n < 8; ++n) samples_[n] = resource(std::to_string(n) + ".sample");
}
void Sound::reset() { tone_ = 0; samplePosition_ = 0; psg_ = Psg(); frame_.clear(); }
void Sound::beginFrame() { frame_.clear(); }
void Sound::speaker(unsigned tone) {
    tone &= 7;
    if (tone != tone_) { tone_ = tone; samplePosition_ = 0; }
}
void Sound::generate(std::size_t count) {
    while (frame_.size() < count) {
        const std::vector<std::uint8_t>& s = samples_[tone_];
        unsigned v = s[samplePosition_] | (s[samplePosition_ + 1] << 8);
        int sample = v < 32768 ? static_cast<int>(v) : static_cast<int>(v) - 65536;
        samplePosition_ = (samplePosition_ + 2) % s.size();
        const int psg = psg_.sample();
        if (melodik_) sample += psg;
        frame_.push_back(static_cast<std::int16_t>(std::max(-32768, std::min(32767, sample))));
    }
}
void Sound::advance(std::uint64_t cycles, unsigned multiplier) {
    generate(std::min<std::size_t>(882, static_cast<std::size_t>(cycles * 882 / (39936 * multiplier))));
}
const std::vector<std::int16_t>& Sound::finishFrame() { generate(882); return frame_; }
}
