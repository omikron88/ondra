// SPDX-License-Identifier: GPL-3.0-or-later
#include <ondra/options.hpp>
#include <ondra/files.hpp>
#include <algorithm>
#include <cstdio>
#include <iostream>
#include <limits>
#include <stdexcept>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
unsigned checks=0;
#define CHECK(expr) do { ++checks; if (!(expr)) throw std::runtime_error(std::string(__func__)+": " #expr+" at "+std::to_string(__LINE__)); } while (false)
typedef ondra::Ds1307::Registers Reg;
void advance(const Reg& before,std::int64_t elapsed,const Reg& after) {
    std::int64_t time=1000000; ondra::Ds1307 rtc([&]() { return time; });
    rtc.write(before); time+=elapsed; CHECK(rtc.read()==after);
}
void registers() {
    const Reg raw={{0xff,0xfe,0xfd,0xfc,0xfb,0xfa,0xf9}};
    advance(raw,999999999999,raw); // CH freezes all seven bytes, even invalid BCD.
    const Reg normal={{0x59,0x59,0x23,7,0x28,2,0x24}};
    advance(normal,999,normal);
    advance(normal,-9999,normal);
    advance(normal,1000,Reg{{0,0,0,1,0x29,2,0x24}});
    advance(Reg{{0x59,0x59,0x23,0,0x28,2,0x25}},1000,Reg{{0,0,0,1,1,3,0x25}});
    advance(Reg{{0x59,0x59,0x23,6,0x29,2,0x24}},1000,Reg{{0,0,0,7,1,3,0x24}});
    advance(Reg{{0x59,0x59,0x23,4,0x31,0x12,0x99}},1000,Reg{{0,0,0,5,1,1,0}});
    advance(Reg{{0x59,0x59,0x51,3,1,1,0x26}},1000,Reg{{0,0,0x72,3,1,1,0x26}}); // 11 AM -> noon.
    advance(Reg{{0x59,0x59,0x71,3,1,1,0x26}},1000,Reg{{0,0,0x52,4,2,1,0x26}}); // 11 PM -> midnight.
    advance(Reg{{0x59,0x59,0x72,3,1,1,0x26}},1000,Reg{{0,0,0x61,3,1,1,0x26}}); // 12 PM -> 1 PM.
    const Reg leap={{0,0,0,1,0x29,2,0x24}};
    advance(leap,INT64_C(36525)*86400000,Reg{{0,0,0,7,0x29,2,0x24}});
    advance(Reg{{1,2,3,7,0x30,4,0x26}},86400000,Reg{{1,2,3,1,1,5,0x26}});
    // The chip accepts invalid dates without silently correcting the write.
    const Reg invalid={{1,2,3,0xf7,0x31,2,0x26}};
    advance(invalid,0,invalid);
    advance(invalid,1000,Reg{{2,2,3,7,0x31,2,0x26}});

    std::int64_t time=1234; ondra::Ds1307 rtc([&]() { return time; });
    rtc.write(normal); time=2233; CHECK(rtc.read()==normal);
    time=2234; CHECK(rtc.read()[0]==0); // Writing seconds resets the divider.
    rtc.write(raw); CHECK(rtc.text().find("stopped")!=std::string::npos);
    const auto state=rtc.state(); ondra::Ds1307 restored([&]() { return time; });
    CHECK(restored.restore(state)); CHECK(restored.state()==state); CHECK(restored.read()==raw);
    CHECK(!restored.restore("broken")); CHECK(!(restored.read()[0]&128));
    CHECK(!restored.restore("0,00,00,00,00,00,00,100"));
    CHECK(!restored.restore("0,00,00,00,00,00,00,-1"));
    // A damaged old timestamp must neither overflow signed arithmetic nor loop for centuries.
    CHECK(restored.restore("-9223372036854775808,00,00,00,01,01,01,00"));
    CHECK(restored.read()[5]>=1 && restored.read()[5]<=0x12);
}
void javaOracle() {
    // JOndra 5b3c850 rtcNow(), with only System.currentTimeMillis replaced
    // by an injected clock: 1,000 valid/invalid BCD and CH/12h/24h cases.
    std::uint32_t seed=0x12345678,hash=0x811c9dc5;
    auto next=[&]() { seed=seed*1664525u+1013904223u; return seed; };
    auto bcd=[](unsigned n) { return static_cast<std::uint8_t>(n/10*16+n%10); };
    std::int64_t now=0; ondra::Ds1307 rtc([&]() { return now; });
    for (unsigned test=0;test<1000;++test) {
        Reg r; for (auto& v:r) v=static_cast<std::uint8_t>(next()>>24);
        if (test%3==0) { r[0]=bcd(r[0]%60); r[1]=bcd(r[1]%60); r[2]=bcd(r[2]%24); r[3]=r[3]%7+1; r[4]=bcd(r[4]%28+1); r[5]=bcd(r[5]%12+1); r[6]=bcd(r[6]%100); }
        if (test%2==0) r[0]&=127;
        now=0; rtc.write(r); now=std::int64_t(next()%(400*86400))*1000+test%1000;
        for (auto v:rtc.read()) hash=(hash^v)*16777619u;
    }
    std::cout<<"Java DS1307 oracle: "<<std::hex<<hash<<std::dec<<'\n'; CHECK(hash==0xf3f8238b);
}
struct Card {
    std::string root="ondra-rtc-test-card";
    Card() {
        CHECK(ondra::canonical(root).empty());
#ifdef _WIN32
        CHECK(_mkdir(root.c_str())==0);
        CHECK(_mkdir((root+"/SUB").c_str())==0);
#else
        CHECK(mkdir(root.c_str(),0700)==0);
        CHECK(mkdir((root+"/SUB").c_str(),0700)==0);
#endif
        ondra::writeFile(root+"/TEST.BIN",{0x42,0xa5});
    }
    ~Card() {
        std::remove((root+"/TEST.BIN").c_str());
#ifdef _WIN32
        _rmdir((root+"/SUB").c_str()); _rmdir(root.c_str());
#else
        rmdir((root+"/SUB").c_str()); rmdir(root.c_str());
#endif
    }
};
struct Uart {
    ondra::SdCard& sd;
    double time=2400000,bit=1996800.0/9600;
    explicit Uart(ondra::SdCard& card):sd(card) { sd.rxLevel(static_cast<std::uint64_t>(time)); send('N'); idle(3000); }
    void idle(double cycles) { time+=cycles; sd.rxLevel(static_cast<std::uint64_t>(time)); }
    void send(unsigned value) {
        sd.txWrite(1,static_cast<std::uint64_t>(time)); idle(bit);
        sd.txWrite(0,static_cast<std::uint64_t>(time)); time+=bit;
        for (unsigned b=0;b<8;++b) { sd.txWrite((value>>b)&1,static_cast<std::uint64_t>(time)); time+=bit; }
        sd.txWrite(1,static_cast<std::uint64_t>(time)); sd.rxLevel(static_cast<std::uint64_t>(time));
    }
    unsigned receive() {
        unsigned limit=100000;
        while (sd.rxLevel(static_cast<std::uint64_t>(time)) && limit) { ++time; --limit; }
        CHECK(limit!=0); unsigned value=0; time+=1.5*bit;
        for (unsigned b=0;b<8;++b) { if (sd.rxLevel(static_cast<std::uint64_t>(time))) value|=1u<<b; time+=bit; }
        CHECK(sd.rxLevel(static_cast<std::uint64_t>(time))); time+=bit/2; return value;
    }
    void writeRtc(const Reg& values) { idle(4000); send('S'); for (auto v:values) send(v); idle(bit); }
    Reg readRtc() { idle(4000); send('T'); Reg r; for (auto& v:r) v=static_cast<std::uint8_t>(receive()); return r; }
};
void serialRtc() {
    Card card; ondra::SdCard sd; sd.configure(card.root);
    std::int64_t time=1000000; sd.rtc()=ondra::Ds1307([&]() { return time; }); Uart bus(sd);
    const Reg raw={{0xff,0xfe,0xfd,0xfc,0xfb,0xfa,0xf9}};
    bus.writeRtc(raw); CHECK(sd.rtc().revision()==1); CHECK(bus.readRtc()==raw);
    time+=90000000; CHECK(bus.readRtc()==raw);
    sd.reset(0); Uart again(sd); CHECK(again.readRtc()==raw); // A CPU/module reset is not a battery reset.
    const Reg running={{0x59,0x59,0x23,7,0x28,2,0x24}};
    again.writeRtc(running); time+=1000; CHECK(again.readRtc()==Reg({{0,0,0,1,0x29,2,0x24}}));
    again.writeRtc(running); again.idle(4000); again.send('T');
    Reg snapshot; snapshot[0]=static_cast<std::uint8_t>(again.receive()); time+=2000;
    for (unsigned n=1;n<7;++n) snapshot[n]=static_cast<std::uint8_t>(again.receive());
    CHECK(snapshot==running); // T latches one coherent register image for all seven UART bytes.
    CHECK(again.readRtc()==Reg({{1,0,0,1,0x29,2,0x24}}));
}
void directoryProtocol() {
    Card card; ondra::SdCard sd; sd.configure(card.root); Uart bus(sd);
    bus.send('D'); std::vector<unsigned> data;
    for (unsigned n=0;n<40;++n) { const auto value=bus.receive(); data.push_back(value); if (value==255) break; }
    CHECK(data.size()==27); CHECK(data[11]==0x51); CHECK(data[24]==0x71); CHECK(data.back()==255);
    // The card remains busy after FF. A command sent at once waits in SBUF;
    // its parameter is then processed at the normal UART speed.
    bus.send('E'); bus.send(0xa5); CHECK(bus.receive()==0xa5);
    bus.idle(5000); bus.send('F'); for (auto c:std::string("TEST    BIN")) bus.send(c);
    const auto start=bus.time; CHECK(bus.receive()==0x42);
    CHECK(bus.time-start>9000); // Two directory entries plus search/dispatch and UART time.
    CHECK(bus.receive()==0xa5);
    bus.idle(5000); bus.send('A'); bus.send(1); bus.idle(bus.bit); bus.bit=1996800.0/57600; bus.idle(5000);
    bus.send('D'); for (unsigned n=0;n<27;++n) bus.receive();
    bus.send('E'); bus.send(0xa5); bus.idle(5000);
    // At 57,600 Bd both bytes arrive during the sector tail. Only E survives
    // in the one-byte SBUF; the chip then waits for a fresh echo parameter.
    bus.send(0x5a); CHECK(bus.receive()==0x5a);
}
void persistence() {
    const std::string path="ondra-rtc-test.ini"; CHECK(ondra::canonical(path).empty());
    struct Cleanup { std::string path; ~Cleanup() { std::remove(path.c_str()); } } cleanup={path};
    const std::string previous="# preserve these settings\nrom=plus\nother=hello\\world\n";
    ondra::writeFile(path,std::vector<std::uint8_t>(previous.begin(),previous.end()));
    ondra::Options options; options.config=path; ondra::Machine first; ondra::configure(first,options);
    const Reg raw={{0xff,0xfe,0xfd,0xfc,0xfb,0xfa,0xf9}};
    first.sd().rtc().write(raw); ondra::finish(first,options);
    const auto data=ondra::readFile(path); const std::string saved(data.begin(),data.end());
    CHECK(saved.find(previous)==0); CHECK(saved.find("sdRtc=")!=std::string::npos);
    ondra::Machine second; ondra::configure(second,options); CHECK(second.sd().rtc().read()==raw);
    second.reset(); CHECK(second.sd().rtc().read()==raw);
    options.noConfig=true; ondra::Machine ignored; ondra::configure(ignored,options); CHECK(!(ignored.sd().rtc().read()[0]&128));
    ignored.sd().rtc().write(Reg{{0x80,0,0,1,1,1,0}}); ondra::finish(ignored,options); CHECK(ondra::readFile(path)==data);
}
}
int main() {
    try { registers(); javaOracle(); serialRtc(); directoryProtocol(); persistence(); std::cout<<"PASS DS1307/OndraSD: "<<checks<<" checks\n"; return 0; }
    catch (const std::exception& e) { std::cerr<<"FAIL RTC: "<<e.what()<<'\n'; return 1; }
}
