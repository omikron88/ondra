// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <ondra/sound.hpp>
#include <ondra/tape.hpp>
#include <ondra/sdcard.hpp>
#include <ondra/printer.hpp>
#include <Z80.h>
#include <array>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace ondra {
enum class Rom { Basic = 0, Tesla = 1, Vili = 2, Plus = 3, Custom = 100 };
class Machine {
public:
    static const unsigned Width = 320, Height = 256, FrameCycles = 312 * 128;
    Machine();
    Machine(const Machine&) = delete;
    Machine& operator=(const Machine&) = delete;
    void selectRom(const std::string& name);
    void customRom(const std::string& a, const std::string& b);
    void reset(bool clearRam = true);
    void nmi() { z80_nmi(&cpu_); }
    void jump(std::uint16_t address);
    std::uint8_t read(std::uint16_t address);
    bool write(std::uint16_t address, std::uint8_t value);
    std::uint8_t input(std::uint16_t port);
    void output(std::uint16_t port, std::uint8_t value);
    void key(unsigned row, unsigned bit, bool down);
    void releaseKeys() { keys_.fill(255); }
    bool runFrame(); // false = execution breakpoint hit, frame remains resumable
    unsigned step();
    void setTurbo(bool enabled);
    bool turbo() const { return multiplier_ == 2; }
    const std::vector<std::uint32_t>& pixels();
    void screenshot(const std::string& path);
    std::uint32_t screenHash();
    void loadBinary(const std::string& path, bool autorun = true);
    void loadRaw(const std::string& path, unsigned address);
    void saveBinary(const std::string& path, unsigned address, unsigned size, int entry = -1) const;
    void loadSnapshot(const std::string& path);
    void saveSnapshot(const std::string& path) const;
    Z80& cpu() { return cpu_; }
    const Z80& cpu() const { return cpu_; }
    std::array<std::uint8_t, 65536>& ram() { return ram_; }
    const std::array<std::uint8_t, 65536>& ram() const { return ram_; }
    Tape& tape() { return tape_; }
    Sound& sound() { return sound_; }
    SdCard& sd() { return sd_; }
    Printer& printer() { return printer_; }
    Bt100& bt100() { return bt100_; }
    std::set<std::uint16_t>& breakpoints() { return breakpoints_; }
    std::uint64_t cycles() const { return cycles_; }
    unsigned resolution() const { return resolution_; }
    std::uint8_t portA0() const { return a0_; }
    std::uint8_t portA3() const { return a3_; }
    bool videoEnabled() const { return (a3_ & 1) != 0; }
    std::string romName() const;
private:
    static zuint8 cpuRead(void*, zuint16);
    static void cpuWrite(void*, zuint16, zuint8);
    static zuint8 cpuIn(void*, zuint16);
    static void cpuOut(void*, zuint16, zuint8);
    static zuint8 interruptAck(void*, zuint16) { return 255; }
    void installRom(Rom type);
    unsigned frameBudget() const;
    std::array<std::uint8_t, 65536> ram_;
    std::array<std::uint8_t, 16384> rom_, custom_;
    std::array<std::uint8_t, 256> keys_;
    Z80 cpu_;
    Rom romType_;
    std::string romName_;
    Tape tape_;
    Sound sound_;
    SdCard sd_;
    Printer printer_;
    Bt100 bt100_;
    std::set<std::uint16_t> breakpoints_;
    std::vector<std::uint32_t> pixels_;
    std::uint64_t cycles_, frameStart_;
    unsigned multiplier_, resolution_, frameTarget_, interruptEnd_, cycleDebt_;
    std::uint8_t a0_, a1_, a3_;
    bool frameActive_;
};
}
