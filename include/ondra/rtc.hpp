// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace ondra {
// DS1307 registers 0-6 as exposed by OndraSD's T/S commands. The battery
// clock follows wall time, independently of CPU speed and computer reset.
class Ds1307 {
public:
    typedef std::array<std::uint8_t,7> Registers;
    typedef std::function<std::int64_t()> Clock; // Unix milliseconds.
    explicit Ds1307(Clock clock=Clock());
    Registers read();
    void write(const Registers& values);
    bool restore(const std::string& state);
    std::string state(); // Java-compatible: baseMs,R0,...,R6 (hex registers).
    std::string text();
    std::uint64_t revision() const { return revision_; }
private:
    void initialize();
    Clock clock_;
    Registers registers_;
    std::int64_t baseMs_;
    std::uint64_t revision_;
    bool loaded_;
};
}
