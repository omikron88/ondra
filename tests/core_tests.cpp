// SPDX-License-Identifier: GPL-3.0-or-later
#include <ondra/machine.hpp>
#include <ondra/files.hpp>
#include <ondra/resources.hpp>
#include <ondra/disassembler.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
unsigned checks = 0;
#define CHECK(expr) do { ++checks; if (!(expr)) throw std::runtime_error(std::string(__func__) + ": " #expr + " at line " + std::to_string(__LINE__)); } while (false)
void rejects(const std::function<void()>& f) {
    bool threw = false;
    try { f(); } catch (const std::exception&) { threw = true; }
    CHECK(threw);
}
struct TempFile {
    std::string path;
    explicit TempFile(const std::string& ext) : path("ondra-test-" + std::to_string(std::rand()) + ext) {}
    ~TempFile() { std::remove(path.c_str()); }
};
void allRam(ondra::Machine& m) { m.output(3, 2); m.ram().fill(0); Z80_PC(m.cpu()) = 0; }
void mapping() {
    ondra::Machine m; m.selectRom("tesla");
    for (unsigned i = 0; i < 2048; ++i) {
        CHECK(m.read(i) == m.read(i + 2048)); CHECK(m.read(0x2000 + i) == m.read(0x3800 + i));
    }
    const auto original = m.read(0);
    CHECK(!m.write(0, original ^ 255)); CHECK(m.read(0) == original);
    m.output(3, 2); CHECK(m.write(0, 42)); CHECK(m.read(0) == 42);
    m.output(3, 0); CHECK(m.read(0) == original); CHECK(m.ram()[0] == 42);
    m.output(3, 6); m.key(0, 4, true);
    CHECK((m.read(0xe000) & 31) == 15); CHECK(m.read(0xe000) == m.read(0xff00));
    CHECK(!m.write(0xe000, 12)); m.key(0, 4, false); CHECK(m.read(0xe000) == 255);
    m.sound().setMelodik(true); CHECK((m.read(0xe00f) & 32) == 0);
    CHECK((m.read(0xe01f) & 32) == 0);
    for (unsigned a=0xe000;a<=0xffff;++a) CHECK(m.read(a)==m.read(0xe000+(a&15)));
    m.output(3, 2); CHECK(m.write(0xe000, 12)); CHECK(m.read(0xe000) == 12);
    m.reset(false); CHECK(m.ram()[0] == 42); CHECK(m.read(0) == original);
    rejects([&]() { m.key(10, 0, true); });
}
void video() {
    ondra::Machine m; allRam(m); m.output(3, 3);
    m.ram()[0xffff] = 0x81; m.ram()[0xfeff] = 0x40;
    const auto& p = m.pixels(); CHECK(p[0] == 0xffffffff); CHECK(p[7] == 0xffffffff);
    CHECK(p[1] == 0xff000000); CHECK(p[9] == 0xffffffff); CHECK(p[255 * 320] == 0xff000000);
    m.input(0x7e); CHECK(m.resolution() == 252);
    m.ram().fill(0); m.ram()[0xff7e] = 0x80; CHECK(m.pixels()[0] == 0xffffffff);
    m.output(3, 0x33); m.input(0x7f); CHECK(m.resolution() == 252);
    m.output(3, 2); CHECK(std::count(m.pixels().begin(), m.pixels().end(), 0xffffffffu) == 0);
}
void cpuAndInterrupts() {
    ondra::Machine m; allRam(m);
    const std::uint8_t program[] = {0x31,0,0xd0, 0x3e,42, 0x32,0,0x40, 0xdd,0x21,0x34,0x12, 0x76};
    std::copy(program, program + sizeof(program), m.ram().begin());
    CHECK(m.step() == 10); CHECK(Z80_SP(m.cpu()) == 0xd000);
    CHECK(m.step() == 7); CHECK(Z80_A(m.cpu()) == 42);
    CHECK(m.step() == 13); CHECK(m.ram()[0x4000] == 42);
    CHECK(m.step() == 14); CHECK(Z80_IX(m.cpu()) == 0x1234);
    m.step(); CHECK(m.cpu().resume == Z80_RESUME_HALT);
    m.nmi(); m.step(); CHECK(Z80_PC(m.cpu()) == 0x66);
    CHECK(Z80_SP(m.cpu()) == 0xcffe); CHECK(m.ram()[0xcffe] == sizeof(program));
    allRam(m); z80_instant_reset(&m.cpu()); Z80_SP(m.cpu()) = 0xd000;
    m.ram()[0] = 0xfb; m.ram()[1] = 0x00; m.cpu().im = 1;
    z80_int(&m.cpu(), 1);
    m.step(); CHECK(Z80_PC(m.cpu()) == 1); m.step(); CHECK(Z80_PC(m.cpu()) == 2);
    m.step(); CHECK(Z80_PC(m.cpu()) == 0x38);
}
void timing() {
    ondra::Machine m; allRam(m);
    CHECK(m.runFrame()); CHECK(m.cycles() == 39936); CHECK(m.sound().finishFrame().size() == 882);
    m.output(3, 3); m.input(0x7e); const auto before = m.cycles();
    m.runFrame(); CHECK(m.cycles() - before == (312 - 252) * 128);
    m.setTurbo(true); const auto beforeTurbo = m.cycles(); m.runFrame();
    CHECK(m.cycles() - beforeTurbo == 2 * (312 - 252) * 128); CHECK(m.sound().finishFrame().size() == 882);
    m.reset(); m.setTurbo(false); allRam(m); m.breakpoints().insert(4);
    CHECK(!m.runFrame()); CHECK(Z80_PC(m.cpu()) == 4);
    m.step(); CHECK(m.runFrame()); CHECK(m.cycles() == 39936);
}
void interruptPulse() {
    for (unsigned mul=1;mul<=2;++mul) {
        ondra::Machine m; allRam(m); m.setTurbo(mul==2); m.breakpoints().insert(0);
        CHECK(!m.runFrame()); CHECK(m.cpu().int_line); CHECK(m.cycles()==0);
        for (unsigned n=0;n<128*mul/4-1;++n) { m.step(); CHECK(m.cpu().int_line); }
        m.step(); CHECK(m.cycles()==128*mul); CHECK(!m.cpu().int_line);
        CHECK(!m.cpu().iff1); // Expiration never depends on accepting INT.
        CHECK(m.runFrame()); CHECK(m.cycles()==39936*mul);

        // EI after the old 16-T window must still accept this frame's INT.
        for (unsigned late=0;late<2;++late) {
            m.reset(); allRam(m); m.breakpoints().clear(); m.cpu().im=1; Z80_SP(m.cpu())=0xd000;
            const unsigned delay=late?128*mul/4+2:12;
            m.jump(0x4000);
            m.ram()[0x4000+delay]=0xfb; m.ram()[0x4000+delay+1]=0; m.ram()[0x4000+delay+2]=0x76;
            const std::uint8_t handler[]={0x04,0xf3,0xc9}; // INC B; DI; RET
            std::copy(handler,handler+sizeof(handler),m.ram().begin()+0x100);
            m.ram()[0x38]=0xc3; m.ram()[0x39]=0; m.ram()[0x3a]=1;
            Z80_B(m.cpu())=0; CHECK(m.runFrame()); CHECK(Z80_B(m.cpu())==(late?0:1));
        }
    }
}
void disassembler() {
    ondra::Machine m; allRam(m);
    auto decode=[&](std::initializer_list<std::uint8_t> bytes,const char* expected,unsigned size) {
        std::copy(bytes.begin(),bytes.end(),m.ram().begin()+0x4000);
        const auto instruction=ondra::disassemble(m,0x4000);
        CHECK(instruction.text==expected); CHECK(instruction.length==size);
    };
    decode({0xdd,0x21,0x34,0x12},"LD IX,1234h",4);
    decode({0xfd,0x36,0xfe,0x42},"LD (IY-02h),42h",4);
    decode({0xdd,0x66,0x80},"LD H,(IX-80h)",3);
    decode({0xfd,0x75,0x01},"LD (IY+01h),L",3);
    decode({0xdd,0xcb,0xff,0x00},"RLC (IX-01h),B",4);
    decode({0xfd,0xcb,0x01,0x7e},"BIT 7,(IY+01h)",4);
    decode({0xed,0x43,0,0x80},"LD (8000h),BC",4);
    decode({0xdd,0xed,0x6a},"ADC HL,HL",3);
    decode({0xdd,0xeb},"EX DE,HL",2);
    decode({0xdd,0xfd,0xe5},"PUSH IY",3);
    decode({0xed,0xb0},"LDIR",2);
    decode({0xcd,0,0x80},"CALL 8000h",3);
    m.ram()[65535]=0x18; m.ram()[0]=0xfe;
    CHECK(ondra::disassemble(m,65535).text=="JR FFFFh");
    m.ram().fill(0xdd); m.jump(0);
    CHECK(ondra::disassemble(m,0).length==16);
    const auto cycles=m.step(); CHECK(cycles>=39936 && cycles<40000);
    CHECK(Z80_PC(m.cpu())!=0); // No infinite loop in debugger step.
}
void binaries() {
    ondra::Machine m; allRam(m);
    TempFile file(".bin");
    m.ram()[0x1234] = 0xaa; m.ram()[0x1235] = 0x55;
    m.saveBinary(file.path, 0x1234, 2, 0x1234); m.ram().fill(0); m.loadBinary(file.path);
    CHECK(m.ram()[0x1234] == 0xaa); CHECK(m.ram()[0x1235] == 0x55); CHECK(Z80_PC(m.cpu()) == 0x1234);
    ondra::writeFile(file.path, {1, 0, 0x40, 1, 0, 42, 1, 0xff, 0xff, 2, 0, 1});
    rejects([&]() { m.loadBinary(file.path); }); CHECK(m.ram()[0x4000] == 0);
    ondra::writeFile(file.path, {}); rejects([&]() { m.loadBinary(file.path); });
    m.ram()[65535] = 91; m.saveBinary(file.path, 0, 65536); m.ram().fill(0); m.loadBinary(file.path);
    CHECK(m.ram()[65535] == 91);
    rejects([&]() { m.loadRaw(file.path, 65535); });
}
void snapshots() {
    ondra::Machine m; m.selectRom("plus");
    for (unsigned n = 0; n < 150; ++n) m.runFrame();
    m.output(3, 7); m.input(0x7e); Z80_AF_(m.cpu()) = 0x1234; Z80_IY(m.cpu()) = 0xabcd;
    const auto hash = m.screenHash(); const auto pc = Z80_PC(m.cpu()); const auto ram = m.ram();
    TempFile file(".osn"); m.saveSnapshot(file.path);
    auto data = ondra::readFile(file.path); CHECK(data.size() == 65536 + 39); CHECK(data[37] == 3);
    m.selectRom("tesla"); m.loadSnapshot(file.path);
    CHECK(m.ram() == ram); CHECK(m.screenHash() == hash); CHECK(Z80_PC(m.cpu()) == pc);
    CHECK(Z80_AF_(m.cpu()) == 0x1234); CHECK(Z80_IY(m.cpu()) == 0xabcd);
    CHECK(m.portA3() == 7); CHECK(m.resolution() == 252);
    m.saveSnapshot(file.path); CHECK(ondra::readFile(file.path) == data);
    // v1 omits the resolution byte.
    data[3] = 1; data.erase(data.begin() + 38); ondra::writeFile(file.path, data); m.loadSnapshot(file.path); CHECK(m.resolution() == 255);
    data.pop_back(); ondra::writeFile(file.path, data); rejects([&]() { m.loadSnapshot(file.path); }); CHECK(m.ram() == ram);
    allRam(m); m.ram()[0] = 0x76; m.step(); m.saveSnapshot(file.path); m.reset(); m.loadSnapshot(file.path);
    CHECK(m.cpu().resume == Z80_RESUME_HALT); CHECK(Z80_PC(m.cpu()) == 1); m.step(); CHECK(Z80_PC(m.cpu()) == 1);
    // Custom banks must travel with an OSN image.
    TempFile bank(".rom"); ondra::writeFile(bank.path, std::vector<std::uint8_t>(2048, 0x5a));
    m.customRom(bank.path, bank.path); m.saveSnapshot(file.path); CHECK(ondra::readFile(file.path)[37] == 100);
    m.selectRom("tesla"); m.loadSnapshot(file.path);
    CHECK(m.read(0) == 0x5a); CHECK(m.read(0x2000) == 0x5a);
}
std::vector<std::uint8_t> cswHeader(unsigned rate, bool initial) {
    const std::string sig = "Compressed Square Wave";
    std::vector<std::uint8_t> d(sig.begin(), sig.end()); d.push_back(0x1a); d.push_back(2); d.push_back(0);
    ondra::putDword(d, rate); ondra::putDword(d, 2); d.push_back(1); d.push_back(initial ? 1 : 0); d.resize(52, 0); return d;
}
void tapes() {
    TempFile file(".csw"); auto data = cswHeader(1000000, false); data.push_back(10); data.push_back(20); ondra::writeFile(file.path, data);
    ondra::Tape tape; tape.load(file.path); CHECK(!tape.level()); tape.advance(20, 1); CHECK(!tape.level()); // motor is off
    tape.setMotor(true); tape.advance(20, 1); CHECK(tape.level()); CHECK(!tape.finished());
    tape.advance(40, 1); CHECK(tape.finished()); tape.rewind(); CHECK(!tape.level());
    tape.advance(20, 2); CHECK(!tape.level()); tape.advance(20, 2); CHECK(tape.level());
    tape.recordLevel(false); tape.startRecording(); tape.advance(20000, 1); tape.recordLevel(true); tape.advance(40000, 1); tape.saveRecording(file.path);
    ondra::Tape recorded; recorded.load(file.path); CHECK(recorded.pulseCount() == 2); CHECK(!recorded.level());
    data = cswHeader(0, false); ondra::writeFile(file.path, data); rejects([&]() { tape.load(file.path); });
    data = cswHeader(22050, true); data.push_back(0); ondra::writeFile(file.path, data); rejects([&]() { tape.load(file.path); });
    std::vector<std::uint8_t> wav = {'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' '};
    ondra::putDword(wav,16); ondra::putWord(wav,1); ondra::putWord(wav,1); ondra::putDword(wav,22050); ondra::putDword(wav,22050);
    ondra::putWord(wav,1); ondra::putWord(wav,8); wav.insert(wav.end(), {'d','a','t','a'}); ondra::putDword(wav,4);
    wav.insert(wav.end(), {0,0,255,255}); ondra::writeFile(file.path,wav); tape.load(file.path); CHECK(tape.pulseCount() == 2);
    std::vector<std::uint8_t> tap(25,0); tap[0]='H'; tap[16]='D'; tap[21]=2;
    tap.insert(tap.end(), {'D',1,2,3}); ondra::writeFile(file.path,tap); tape.load(file.path); CHECK(tape.pulseCount() > 3000);
    tap.back() = 4; ondra::writeFile(file.path,tap); rejects([&]() { tape.load(file.path); });
}
void romTapeLoader() {
    // A complete, synthetic .K file: load JP 4000h at 4000h, then run it.
    // This exercises the ROM's actual biphase decoder, header checksums, data
    // checksums, motor/IO latches and entry-point loader, rather than just our
    // TAP parser. The old nominal 500-kHz Java timing fails this test.
    const std::uint8_t program[] = {1,0,0x40,3,0,0xc3,0,0x40,2,0,0x40};
    std::vector<std::uint8_t> binary(1024,0);
    std::copy(program,program+sizeof(program),binary.begin());
    std::vector<std::uint8_t> tap(25, ' ');
    tap.reserve(25 + 2 + binary.size());
    const std::string name = "PORTTEST";
    std::copy(name.begin(), name.end(), tap.begin() + 1);
    tap[0]='H'; tap[13]='.'; tap[14]='K'; tap[15]=0xef; tap[16]='D';
    tap[19]='1'; tap[20]=0; tap[21]=0; tap[22]=4; tap[23]=0;
    unsigned checksum = 0;
    for (unsigned i=1;i<24;++i) checksum += tap[i];
    tap[24] = checksum & 255;
    tap.push_back('D'); tap.insert(tap.end(),binary.begin(),binary.end());
    checksum = 0; for (auto byte : binary) checksum += byte; tap.push_back(checksum & 255);
    TempFile file(".tap"); ondra::writeFile(file.path,tap);
    for (const std::string rom : {"vili25","vili27","plus"}) {
        std::cout << "Checking ROM TAP loader: " << rom << '\n';
        ondra::Machine m; m.selectRom(rom);
        for (unsigned n=0;n<150;++n) m.runFrame();
        m.tape().load(file.path);
        if (rom == "plus") {
            // Ondra+ boots to a menu; its second item is "Zavest z pasky".
            m.key(8,2,true); for (unsigned n=0;n<3;++n) m.runFrame();
            m.releaseKeys(); for (unsigned n=0;n<3;++n) m.runFrame();
        }
        m.key(5,4,true);
        for (unsigned n=0;n<3;++n) m.runFrame();
        m.releaseKeys();
        for (unsigned n=0;n<1000 && Z80_PC(m.cpu())!=0x4000;++n) m.runFrame();
        CHECK(m.ram()[0x4000] == 0xc3); CHECK(m.ram()[0x4002] == 0x40);
        CHECK(Z80_PC(m.cpu()) == 0x4000); CHECK(!m.tape().motor());
    }
}
void audio() {
    ondra::Sound sound;
    CHECK(sound.finishFrame() == std::vector<std::int16_t>(882,0));
    sound.beginFrame(); sound.advance(39936/2,1); sound.speaker(3);
    const auto samples = sound.finishFrame(); CHECK(samples.size() == 882);
    CHECK(std::count(samples.begin(), samples.begin()+441, 0) == 441);
    CHECK(std::count(samples.begin()+441, samples.end(), 0) != 441);
    ondra::Psg psg; CHECK(psg.sample() == 0);
    psg.write(0x84); psg.write(0x04); psg.write(0x90);
    int changes = 0, previous = psg.sample();
    for (unsigned n = 0; n < 4410; ++n) { int next = psg.sample(); if (next != previous) ++changes; previous = next; }
    CHECK(changes > 100 && changes < 300); // ~919 Hz with divider 68.
    psg.write(0x9f); CHECK(psg.sample() == 0);
}
void sdcard() {
    const std::string root = "ondra-test-card-" + std::to_string(std::rand());
#ifdef _WIN32
    CHECK(_mkdir(root.c_str()) == 0);
#else
    CHECK(mkdir(root.c_str(), 0700) == 0);
#endif
    struct Cleanup {
        std::string root;
        ~Cleanup() { std::remove((root+"/TEST.BIN").c_str());
#ifdef _WIN32
            _rmdir(root.c_str());
#else
            rmdir(root.c_str());
#endif
        }
    } cleanup = {root};
    ondra::writeFile(root+"/TEST.BIN",{0x42,0xa5});
    ondra::SdCard sd; sd.configure(root);
    double clock = 2400000, bit = 1996800.0/9600;
    // Consume the power-on CR and send N after the 1.1 second startup delay.
    sd.rxLevel(static_cast<std::uint64_t>(clock));
    auto tick = [&]() { sd.rxLevel(static_cast<std::uint64_t>(clock)); };
    auto send = [&](unsigned value) {
        sd.txWrite(1,static_cast<std::uint64_t>(clock)); clock += bit; tick();
        sd.txWrite(0,static_cast<std::uint64_t>(clock)); clock += bit;
        for (unsigned b=0;b<8;++b) { sd.txWrite((value>>b)&1,static_cast<std::uint64_t>(clock)); clock += bit; }
        // The module can answer during the stop bit. Start listening now;
        // the next send() supplies a full idle/stop bit before its start bit.
        sd.txWrite(1,static_cast<std::uint64_t>(clock)); tick();
    };
    auto receive = [&]() {
        unsigned limit = 100000;
        while (sd.rxLevel(static_cast<std::uint64_t>(clock)) && limit) { clock += 1; --limit; }
        CHECK(limit != 0);
        unsigned value = 0; clock += 1.5*bit;
        for (unsigned b=0;b<8;++b) { if (sd.rxLevel(static_cast<std::uint64_t>(clock))) value |= 1u<<b; clock += bit; }
        CHECK(sd.rxLevel(static_cast<std::uint64_t>(clock))); clock += bit/2;
        return value;
    };
    send('N'); send('E'); send(0xa5); CHECK(receive() == 0xa5);
    send('K'); CHECK(receive() == 0x12);
    send('F'); for (unsigned char c : std::string("TEST    BIN")) send(c);
    CHECK(receive() == 0x42); CHECK(receive() == 0xa5);
    CHECK(ondra::inside(ondra::canonical(root+"/TEST.BIN"),ondra::canonical(root)));
    CHECK(!ondra::inside(ondra::canonical(root)+"-other/file",ondra::canonical(root)));
    send('A'); send(1); clock += bit; tick(); bit = 1996800.0/57600;
    send('E'); send(0x3c); CHECK(receive() == 0x3c);
}
}
int main() {
    const std::pair<const char*, void(*)()> tests[] = {{"memory mapping",mapping},{"video",video},{"Z80/interrupts",cpuAndInterrupts},
        {"frame timing/breakpoints",timing},{"128/256-T INT pulse",interruptPulse},{"Z80 disassembler/prefix limits",disassembler},{"BIN files",binaries},{"OSN snapshots",snapshots},{"tape formats",tapes},
        {"ROM TAP loader",romTapeLoader},{"speaker/PSG",audio},{"OndraSD UART",sdcard}};
    try {
        for (const auto& test : tests) { test.second(); std::cout << "PASS " << test.first << '\n'; }
        std::cout << sizeof(tests)/sizeof(tests[0]) << " test groups, " << checks << " checks passed\n"; return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
