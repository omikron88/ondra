// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <ondra/rtc.hpp>
#include <deque>
#include <string>
#include <vector>
namespace ondra {
// Bit-level UART implementation of JOndra's OndraSD 2.2 + RTC model.
class SdCard {
public:
    SdCard();
    void configure(const std::string& root);
    bool enabled() const { return !root_.empty(); }
    void reset(std::uint64_t clock);
    void frameStart(std::uint64_t clock, unsigned multiplier);
    void txWrite(unsigned level, std::uint64_t clock);
    bool rxLevel(std::uint64_t clock);
    Ds1307& rtc() { return rtc_; }
private:
    typedef std::int64_t Time;
    struct Tx { Time start; double bit; unsigned value; Time end() const; };
    struct Entry { std::string fat, path; bool dir; std::uint64_t size; };
    Time real(std::uint64_t clock) const;
    double bitTime() const;
    double gapTime() const;
    void restart(Time t);
    void advance(Time t);
    void received(unsigned value, Time t);
    void process(unsigned value, Time t);
    void send(unsigned value, Time t);
    void pause(unsigned cycles);
    void sendFile(const std::string& path, Time t, bool tap);
    void sendDirectory(Time t);
    void changeDirectory(const std::string& name);
    void sendRtc(Time t);
    void setRtc();
    Time search(const std::string& name,Time time);
    std::vector<Entry> list(const std::string& path) const;
    std::string find(const std::string& path, const std::string& fat, bool dir) const;
    std::string root_, current_;
    std::deque<Tx> queue_;
    std::vector<unsigned> params_;
    Time frameClock_, frameReal_, nextBroadcast_, free_, lastEnd_, rxStart_, pendingTime_;
    unsigned multiplier_, baud_, delay_, txLine_, rxBit_, rxShift_;
    int command_, pending_;
    bool frameStarted_, broadcast_, waitHigh_, rxActive_;
    double rxBitTime_;
    Ds1307 rtc_;
};
}
