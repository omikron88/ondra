// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace ondra {
// Centronics interface: append unmodified bytes, model ACK and the print buffer.
class Printer {
public:
    Printer();
    void configure(const std::string& path);
    void reset(std::uint64_t time);
    void advance(std::uint64_t time,unsigned multiplier);
    void changeSpeed(std::uint64_t time,unsigned before,unsigned after);
    void strobe(std::uint8_t data,std::uint64_t time,unsigned multiplier);
    bool busy(std::uint64_t time) const { return time<busyUntil_; }
    bool enabled() const { return enabled_; }
    const std::string& path() const { return path_; }
    const std::string& error() const { return error_; }
    std::uint64_t total() const { return total_; }
    void flush();
private:
    std::ofstream output_;
    std::string path_,error_;
    bool enabled_;
    double queued_;
    std::uint64_t lastTime_,busyUntil_,total_;
};

// Tesla BT100 single-needle printer, including the joystick/keyboard sensors.
// Timing, polarities and carriage backlash follow JOndra's measured model.
class Bt100 {
public:
    static const unsigned Width=600,MaxRows=100000;
    Bt100();
    void configure(bool enabled) { enabled_=enabled; }
    bool enabled() const { return enabled_; }
    bool powered() const { return power_; }
    void power(bool on,std::uint64_t time,unsigned multiplier);
    void reset(std::uint64_t time);
    void advance(std::uint64_t time,unsigned multiplier);
    void changeSpeed(std::uint64_t time,unsigned before,unsigned after);
    void write(std::uint8_t value,std::uint64_t time,unsigned multiplier);
    unsigned sensors(std::uint64_t time,unsigned multiplier);
    unsigned sensorLines(std::uint64_t time,unsigned multiplier);
    unsigned row() const { return row_; }
    double carriage() const { return x_; }
    bool needleDown() const { return !(output_&1); }
    unsigned paperRows() const;
    bool paperFull() const { return row_>=MaxRows; }
    bool dot(unsigned row,unsigned column) const;
    void newPaper();
    void savePng(const std::string& path,unsigned scale=2) const;
private:
    void strike();
    bool enabled_,power_;
    std::uint8_t output_;
    double x_,feed_;
    unsigned row_;
    std::uint64_t lastTime_,moveFrom_;
    std::vector<std::array<std::uint8_t,Width/8> > paper_;
};
}
