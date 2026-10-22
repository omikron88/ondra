// SPDX-License-Identifier: GPL-3.0-or-later
#include <ondra/options.hpp>
#include <ondra/files.hpp>
#include <iostream>
#include <iomanip>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace ondra {
unsigned number(const std::string& text, unsigned maximum) {
    std::size_t used = 0; unsigned long value;
    if (text.empty() || text[0] == '-') throw std::runtime_error("Invalid number: " + text);
    // Decimal by default; hexadecimal addresses use 0x or $.
    const std::string s = text[0] == '$' ? "0x" + text.substr(1) : text;
    try { value = std::stoul(s, &used, s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X') ? 16 : 10); }
    catch (const std::exception&) { throw std::runtime_error("Invalid number: " + text); }
    if (used != s.size() || value > maximum) throw std::runtime_error("Number out of range: " + text);
    return static_cast<unsigned>(value);
}
Options parseOptions(int argc, char** argv) {
#ifdef _WIN32
    // SDL drop events use UTF-8; use the same encoding for command-line paths.
    int wideCount = 0;
    LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &wideCount);
    if (!wide) throw std::runtime_error("Cannot read Windows command line");
    std::vector<std::string> arguments;
    try { for (int i = 0; i < wideCount; ++i) arguments.push_back(utf8(wide[i])); }
    catch (...) { LocalFree(wide); throw; }
    LocalFree(wide);
    std::vector<char*> pointers;
    for (auto& argument : arguments) pointers.push_back(&argument[0]);
    argc = wideCount; argv = pointers.data();
#endif
    Options o;
    for (int n = 1; n < argc; ++n) {
        const std::string arg = argv[n];
        o.explicitOptions.insert(arg);
        auto value = [&]() -> std::string { if (n + 1 >= argc) throw std::runtime_error("Missing value for " + arg); return argv[++n]; };
        if (arg == "--rom") o.rom = value();
        else if (arg == "--rom-a") o.romA = value();
        else if (arg == "--rom-b") o.romB = value();
        else if (arg == "--load") o.load = value();
        else if (arg == "--raw") { o.load = value(); o.raw = true; }
        else if (arg == "--address") o.address = number(value());
        else if (arg == "--pc") o.pc = static_cast<int>(number(value()));
        else if (arg == "--load-after") o.loadAfter = number(value(), 1000000);
        else if (arg == "--tape") o.tape = value();
        else if (arg == "--record") o.record = value();
        else if (arg == "--snapshot") o.snapshot = value();
        else if (arg == "--save-snapshot") o.saveSnapshot = value();
        else if (arg == "--screenshot") o.screenshot = value();
        else if (arg == "--sd") o.sd = value();
        else if (arg == "--printer") o.printer = value();
        else if (arg == "--bt100") o.bt100 = o.bt100Power = true;
        else if (arg == "--bt100-output") { o.bt100Output = value(); o.bt100 = o.bt100Power = true; }
        else if (arg == "--config") o.config = value();
        else if (arg == "--no-config") o.noConfig = true;
        else if (arg == "--ui-screenshot") o.uiScreenshot = value();
        else if (arg == "--frames") o.frames = number(value(), 10000000);
        else if (arg == "--scale") { o.scale = number(value(), 8); if (!o.scale) throw std::runtime_error("Scale must be 1..8"); }
        else if (arg == "--turbo") o.turbo = true;
        else if (arg == "--melodik") o.melodik = true;
        else if (arg == "--mute") o.mute = true;
        else if (arg == "--check-boot") o.checkBoot = true;
        else if (arg == "--debug") o.debug = true;
        else if (arg == "--help" || arg == "-h") o.help = true;
        else throw std::runtime_error("Unknown option: " + arg);
    }
    if (o.romA.empty() != o.romB.empty()) throw std::runtime_error("Use --rom-a and --rom-b together");
    if (unsigned(o.melodik)+unsigned(!o.printer.empty())+unsigned(o.bt100)>1)
        throw std::runtime_error("Melodik, Centronics and BT100 share one port; choose only one");
    if (!o.load.empty() && o.frames && o.loadAfter >= o.frames) throw std::runtime_error("--load-after must be less than --frames");
    return o;
}
void configure(Machine& m, const Options& o) {
    m.selectRom(o.rom);
    if (!o.romA.empty()) m.customRom(o.romA, o.romB);
    m.setTurbo(o.turbo); m.sound().setMelodik(o.melodik);
    m.printer().configure(o.printer); m.bt100().configure(o.bt100);
    m.bt100().power(o.bt100Power,m.cycles(),m.turbo()?2:1);
    if (!o.sd.empty()) m.sd().configure(o.sd);
    if (!o.snapshot.empty()) m.loadSnapshot(o.snapshot);
    if (!o.tape.empty()) m.tape().load(o.tape);
    if (!o.record.empty()) m.tape().startRecording();
    if (o.pc >= 0 && o.load.empty()) m.jump(static_cast<std::uint16_t>(o.pc));
}
void loadProgram(Machine& m, const Options& o) {
    if (o.load.empty()) return;
    if (o.raw) m.loadRaw(o.load, o.address); else m.loadBinary(o.load);
    if (o.pc >= 0) m.jump(static_cast<std::uint16_t>(o.pc));
}
void finish(Machine& m, const Options& o) {
    if (!o.saveSnapshot.empty()) m.saveSnapshot(o.saveSnapshot);
    if (!o.record.empty()) m.tape().saveRecording(o.record);
    if (!o.screenshot.empty()) m.screenshot(o.screenshot);
    m.printer().flush();
    if (!o.bt100Output.empty()) { m.bt100().advance(m.cycles(),m.turbo()?2:1); m.bt100().savePng(o.bt100Output); }
}
void printRegisters(const Machine& m) {
    const Z80& c = m.cpu();
    std::cout << std::hex << std::setfill('0') << "PC=" << std::setw(4) << Z80_PC(c)
        << " SP=" << std::setw(4) << Z80_SP(c) << " AF=" << std::setw(4) << Z80_AF(c)
        << " BC=" << std::setw(4) << Z80_BC(c) << " DE=" << std::setw(4) << Z80_DE(c)
        << " HL=" << std::setw(4) << Z80_HL(c) << " IX=" << std::setw(4) << Z80_IX(c)
        << " IY=" << std::setw(4) << Z80_IY(c) << std::dec << " IM=" << unsigned(c.im)
        << " IFF=" << unsigned(c.iff1) << '/' << unsigned(c.iff2) << " T=" << m.cycles()
        << " A0=" << std::hex << std::setw(2) << unsigned(m.portA0()) << " A3=" << std::setw(2) << unsigned(m.portA3()) << std::dec << '\n';
}
void printHelp() {
    std::cout << "Ondra SPO 186 emulator (C++11, redcode/Z80)\n"
        "  --rom basic|tesla|vili25|vili27|plus (default: basic)\n"
        "  --rom-a FILE --rom-b FILE   custom 2/4/8 KiB ROM banks\n"
        "  --load FILE.bin            load OndraSD BIN after ROM boot\n"
        "  --raw FILE --address N     load raw bytes into physical RAM\n"
        "  --load-after N             load after N frames (default: 100)\n"
        "  --pc N                     override entry point (0x prefix for hex)\n"
        "  --tape FILE                play Ondra TAP, CSW RLE or mono PCM WAV\n"
        "  --record FILE.csw          record tape output; saved on exit\n"
        "  --sd DIRECTORY             enable OndraSD + RTC\n"
        "  --printer FILE             append raw Centronics printer output\n"
        "  --bt100                    connect and power on Tesla BT100\n"
        "  --bt100-output FILE.png    power on BT100 and save paper on exit\n"
        "                            (printers and Melodik share one port)\n"
        "  --snapshot FILE.osn        load JOndra OSN v1/v2\n"
        "  --save-snapshot FILE.osn   save OSN v2 on exit\n"
        "  --screenshot FILE.ppm      save the last framebuffer\n"
        "  --ui-screenshot FILE.bmp   capture the desktop UI on exit (GUI)\n"
        "  --config FILE --no-config  settings file / ignore saved UI settings\n"
        "  --turbo --melodik --mute --scale 1..8\n"
        "  --frames N                 exit after N frames (headless default: 150)\n"
        "  --check-boot               require enabled video and visible pixels\n"
        "  --debug                    terminal debugger (ondra-headless only)\n"
        "Keys: F1 about/help, F2 pause, F3 step, F4 NMI, F5 reset (Shift: cold),\n"
        "      F6 turbo, F7 step over, F8 save snapshot, F9 open snapshot,\n"
        "      F10 quit, F11 fullscreen, F12 save screenshot, Home rewind tape.\n"
        "Ondra modifiers: Shift, Alt=Symbols, Ctrl, Tab=Numbers, Equals=CS.\n";
}
}
