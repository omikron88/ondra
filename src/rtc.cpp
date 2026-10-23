// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from JOndra OndraSD.java at 5b3c850e63a8573ea1a24ae3a8e14b6f91f1ce38.
#include <ondra/rtc.hpp>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace ondra {
namespace {
std::int64_t wallTime() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
unsigned bcd(unsigned value) { return (value/10)*16+value%10; }
unsigned unbcd(unsigned value) { return (value>>4)*10+(value&15); }
unsigned monthDays(unsigned month,unsigned year) {
    switch (month) {
    case 2: return year%4==0?29:28;
    case 4: case 6: case 9: case 11: return 30;
    default: return 31;
    }
}
}
Ds1307::Ds1307(Clock clock):clock_(clock?clock:Clock(wallTime)),registers_(),baseMs_(0),revision_(0),loaded_(false) {}
void Ds1307::initialize() {
    if (loaded_) return;
    const auto now=clock_(); const auto seconds=static_cast<std::time_t>(now/1000);
    const std::tm* date=std::localtime(&seconds);
    if (date) {
        registers_={{static_cast<std::uint8_t>(bcd(date->tm_sec)),static_cast<std::uint8_t>(bcd(date->tm_min)),
            static_cast<std::uint8_t>(bcd(date->tm_hour)),static_cast<std::uint8_t>(date->tm_wday),
            static_cast<std::uint8_t>(bcd(date->tm_mday)),static_cast<std::uint8_t>(bcd(date->tm_mon+1)),
            static_cast<std::uint8_t>(bcd((date->tm_year+1900)%100))}};
    } else registers_={{0,0,0,0,1,1,0}};
    baseMs_=now-now%1000; loaded_=true;
}
void Ds1307::write(const Registers& values) {
    registers_=values; baseMs_=clock_(); loaded_=true; ++revision_;
}
Ds1307::Registers Ds1307::read() {
    initialize(); auto r=registers_; const auto now=clock_();
    if ((r[0]&128) || now<=baseMs_) return r;
    // Unsigned subtraction also handles extreme persisted timestamps safely.
    const auto elapsed=(static_cast<std::uint64_t>(now)-static_cast<std::uint64_t>(baseMs_))/1000;
    if (!elapsed) return r;
    const auto seconds=unbcd(r[0]&127)+elapsed;
    r[0]=static_cast<std::uint8_t>(bcd(static_cast<unsigned>(seconds%60)));
    const auto minutes=unbcd(r[1]&127)+seconds/60;
    r[1]=static_cast<std::uint8_t>(bcd(static_cast<unsigned>(minutes%60)));
    const bool h12=(r[2]&64)!=0;
    const auto hours=(h12?unbcd(r[2]&31)%12+((r[2]&32)?12:0):unbcd(r[2]&63))+minutes/60;
    const unsigned hour=static_cast<unsigned>(hours%24);
    r[2]=static_cast<std::uint8_t>(h12?(64|(hour>=12?32:0)|bcd(hour%12?hour%12:12)):bcd(hour));
    std::uint64_t days=hours/24;
    unsigned weekday=r[3]&7,day=unbcd(r[4]&63),month=unbcd(r[5]&31),year=unbcd(r[6]);
    if (days) weekday=weekday?static_cast<unsigned>((weekday-1+days%7)%7)+1:static_cast<unsigned>((days-1)%7)+1;
    while (days) {
        const unsigned length=monthDays(month,year);
        if (day>=1 && day<=length && month>=1 && month<=12 && year<100) {
            // The two-digit calendar repeats every 36,525 days. Skip cycles
            // and whole months rather than looping over an old timestamp.
            days%=36525;
            if (days<length-day+1) { day+=static_cast<unsigned>(days); break; }
            days-=length-day+1; day=1;
            if (++month>12) { month=1; year=(year+1)%100; }
        } else {
            --days;
            if (++day>length) { day=1; if (++month>12) { month=1; year=(year+1)%100; } }
        }
    }
    r[3]=static_cast<std::uint8_t>(weekday); r[4]=static_cast<std::uint8_t>(bcd(day));
    r[5]=static_cast<std::uint8_t>(bcd(month)); r[6]=static_cast<std::uint8_t>(bcd(year));
    return r;
}
bool Ds1307::restore(const std::string& state) {
    loaded_=false; revision_=0;
    try {
        std::istringstream input(state); std::vector<std::string> fields; std::string field;
        while (std::getline(input,field,',')) fields.push_back(field);
        if (fields.size()!=8) return false;
        std::size_t used=0; const auto base=std::stoll(fields[0],&used,10);
        if (used!=fields[0].size()) return false;
        Registers values;
        for (unsigned n=0;n<7;++n) {
            if (fields[n+1].empty() || fields[n+1][0]=='-') return false;
            const auto value=std::stoul(fields[n+1],&used,16);
            if (used!=fields[n+1].size() || value>255) return false;
            values[n]=static_cast<std::uint8_t>(value);
        }
        registers_=values; baseMs_=base; loaded_=true; return true;
    } catch (const std::exception&) { return false; }
}
std::string Ds1307::state() {
    initialize(); std::ostringstream out; out<<baseMs_<<std::hex<<std::uppercase<<std::setfill('0');
    for (auto value:registers_) out<<','<<std::setw(2)<<static_cast<unsigned>(value);
    return out.str();
}
std::string Ds1307::text() {
    const auto r=read(); const bool h12=(r[2]&64)!=0; char buffer[100];
    std::snprintf(buffer,sizeof(buffer),"%02X.%02X.20%02X %02X:%02X:%02X%s%s",r[4],r[5],r[6],
        r[2]&(h12?31:63),r[1],r[0]&127,h12?((r[2]&32)?" PM":" AM"):"",(r[0]&128)?" (stopped)":"");
    return buffer;
}
}
