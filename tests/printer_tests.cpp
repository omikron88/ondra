// SPDX-License-Identifier: GPL-3.0-or-later
#include <ondra/machine.hpp>
#include <ondra/files.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <stdexcept>

namespace {
unsigned checks=0;
#define CHECK(value) do { ++checks; if (!(value)) throw std::runtime_error(std::string(__func__)+": " #value+" at "+std::to_string(__LINE__)); } while (false)
struct File {
    std::string path;
    explicit File(const char* name):path(name) { CHECK(ondra::canonical(path).empty()); }
    ~File() { std::remove(path.c_str()); }
};
void centronics() {
    File file("ondra-test-centronics.prn"); ondra::writeFile(file.path,{42});
    ondra::Machine m; m.printer().configure(file.path); m.output(3,6); m.ram().fill(0);
    CHECK(!(m.read(0xe00f)&32)); CHECK(!(m.read(0xff01)&32));
    // Real Z80 driver: wait for BUSY, latch data, pulse /STROBE, repeat.
    const std::uint8_t program[]={0x21,0,0x41,0x06,6,0x3a,0x0f,0xe0,0xe6,0x20,0x20,0xf9,
        0x3e,8,0xd3,0x0a,0x7e,0xd3,9,0xaf,0xd3,0x0a,0x23,0x10,0xec,0x76};
    const std::vector<std::uint8_t> data={'A',0,13,10,255,'Z'};
    std::copy(program,program+sizeof(program),m.ram().begin()+0x4000);
    std::copy(data.begin(),data.end(),m.ram().begin()+0x4100); m.jump(0x4000);
    unsigned limit=20000;
    while (m.cpu().resume!=Z80_RESUME_HALT && limit) { m.step(); --limit; }
    CHECK(limit!=0); CHECK(m.printer().total()==data.size()); CHECK(m.read(0xe00f)&32);
    std::vector<std::uint8_t> expected={42}; expected.insert(expected.end(),data.begin(),data.end());
    CHECK(ondra::readFile(file.path)==expected); // Binary append, including NUL/CR/LF/FF.
    for (unsigned n=0;n<60;++n) m.step();
    CHECK(!(m.read(0xe00f)&32));
    m.output(0x1a,8); m.output(0x19,'Q'); m.output(0x1a,0); m.output(0x1a,0);
    CHECK(m.printer().total()==data.size()+1); CHECK(ondra::readFile(file.path).back()=='Q');
    m.reset(); CHECK(m.printer().total()==data.size()+1); CHECK(!m.printer().busy(0));
    m.printer().configure(""); m.output(0x1a,8); m.output(0x1a,0); CHECK(m.printer().total()==data.size()+1);

    ondra::Printer p; p.configure(file.path);
    for (unsigned n=0;n<2049;++n) p.strobe(0,0,1);
    CHECK(p.busy(9999)); CHECK(!p.busy(10000));
    p.strobe(1,2000000,1); CHECK(p.busy(2000199)); CHECK(!p.busy(2000200));
    p.reset(0); p.strobe(2,0,1); p.changeSpeed(100,1,2); CHECK(p.busy(299)); CHECK(!p.busy(300));
    p.reset(0); p.strobe(3,0,2); CHECK(p.busy(399)); CHECK(!p.busy(400));
    p.configure(file.path+"/unwritable.prn"); p.strobe(0,0,1);
    CHECK(!p.enabled()); CHECK(!p.error().empty());
    bool failed=false; try { p.flush(); } catch (const std::exception&) { failed=true; } CHECK(failed);
    p.configure(file.path); p.strobe(4,0,1); p.flush(); CHECK(p.error().empty());
}
void bt100() {
    ondra::Bt100 p; p.configure(true); CHECK(p.sensorLines(0,1)==15);
    p.power(true,0,1); CHECK(p.sensorLines(0,1)==12);
    p.write(0xfb,0,1); p.advance(3000,1); CHECK(p.carriage()==0);
    p.advance(7000,1); CHECK(p.carriage()==1); CHECK(p.sensorLines(7000,1)==9);
    p.write(0xfa,7000,1); CHECK(p.dot(0,1));
    p.reset(0); CHECK(p.dot(0,1)); CHECK(p.carriage()==1); CHECK(p.powered());
    p.newPaper(); CHECK(!p.dot(0,1)); CHECK(p.carriage()==1);
    p.write(0x7f,0,1); p.advance(3000,1); p.write(0x7e,3000,1);
    CHECK(p.dot(0,3)); // Leftward backlash puts the strike two columns to the right.
    p.power(false,3000,1); p.write(0xfb,3000,1); p.advance(1000000,1); CHECK(p.carriage()==1);
    CHECK(p.sensorLines(1000000,1)==15);
    p.power(true,1000000,1); p.advance(1003000,1); CHECK(p.carriage()==1);
    p.advance(1007000,1); CHECK(p.carriage()==2);
    p.write(0xef,1007000,1); CHECK((p.sensorLines(1010000,1)&8)==0);
    CHECK((p.sensorLines(1015000,1)&8)!=0); p.advance(1017000,1); CHECK(p.row()==1);
    p.write(0xff,1017000,1); p.write(0xfe,1017000,1); CHECK(p.dot(1,2));
    p.newPaper(); CHECK(p.row()==0); CHECK(p.paperRows()==1); CHECK(!p.dot(1,2));
    p.reset(0); p.write(0xfb,0,1); p.changeSpeed(1000,1,2); p.advance(5000,2); CHECK(p.carriage()==2);
    p.advance(13000,2); CHECK(p.carriage()==3);
    p.write(0xef,13000,2); p.advance(13000+std::uint64_t(ondra::Bt100::MaxRows+10)*20000,2);
    CHECK(p.paperFull()); CHECK(p.paperRows()==ondra::Bt100::MaxRows); p.newPaper(); CHECK(!p.paperFull());

    ondra::Machine m; m.output(3,6); m.bt100().configure(true);
    CHECK((m.read(0xe009)&15)==15); m.bt100().power(true,0,1);
    CHECK((m.read(0xe009)&15)==12); CHECK((m.read(0xff00)&15)==12); CHECK((m.read(0xe00f)&15)==15);
    m.key(0,3,true); CHECK((m.read(0xe000)&15)==4);
    m.key(9,2,true); CHECK((m.read(0xe009)&15)==12); // Joystick row is replaced by sensors.
    m.output(0x19,0xfe); CHECK(m.bt100().dot(0,0));
    m.output(3,2); m.ram()[0xe009]=42; CHECK(m.read(0xe009)==42); // No I/O mapping, no sensors.
    m.reset(); CHECK(m.bt100().dot(0,0)); CHECK(!m.bt100().needleDown());
}
void referenceTrace() {
    // Oracle: unmodified Java BT100.java at 7aad67e, 16 alternating sweeps.
    // Compare every sampled sensor and every paper dot, at 2 MHz and turbo.
    for (unsigned mul=1;mul<=2;++mul) {
        ondra::Bt100 p; p.configure(true); p.power(true,0,mul);
        std::uint64_t t=0; std::uint32_t sensors=0x811c9dc5,dots=0x811c9dc5;
        for (unsigned row=0;row<16;++row) {
            const unsigned out=row%2==0?0xfb:0x7f; p.write(out,t,mul);
            for (unsigned n=0;n<1100;++n) {
                t+=2000*mul; sensors=(sensors^p.sensorLines(t,mul))*16777619u;
                p.write(n%3==0?out&~1:out,t,mul);
            }
            p.write(0xef,t,mul); t+=10000*mul; sensors=(sensors^p.sensorLines(t,mul))*16777619u; p.write(255,t,mul);
        }
        for (unsigned row=0;row<p.paperRows();++row) for (unsigned col=0;col<ondra::Bt100::Width;++col) dots=(dots^unsigned(p.dot(row,col)))*16777619u;
        std::cout<<"BT100 reference: sensors="<<std::hex<<sensors<<" dots="<<dots<<std::dec<<'\n';
        CHECK(p.row()==16); CHECK(p.paperRows()==17); CHECK(p.carriage()==-0.75);
        CHECK(sensors==0x22c613e3); CHECK(dots==0x81167172);
    }
}
}
int main() {
    try { centronics(); bt100(); referenceTrace(); std::cout<<"PASS printers: "<<checks<<" checks\n"; return 0; }
    catch (const std::exception& e) { std::cerr<<"FAIL printers: "<<e.what()<<'\n'; return 1; }
}
