// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <ondra/machine.hpp>
#include <set>
#include <string>
namespace ondra {
struct Options {
    std::string rom = "basic", romA, romB, load, tape, record, snapshot, saveSnapshot, screenshot, sd, config, uiScreenshot, printer, bt100Output, sdRtc;
    std::set<std::string> explicitOptions;
    unsigned frames = 0, loadAfter = 100, scale = 2, address = 0;
    int pc = -1;
    bool raw = false, turbo = false, melodik = false, mute = false, checkBoot = false, debug = false, help = false, noConfig = false;
    bool bt100 = false, bt100Power = false;
};
unsigned number(const std::string& value, unsigned maximum = 65535);
Options parseOptions(int argc, char** argv);
void configure(Machine& machine, const Options& options);
void loadProgram(Machine& machine, const Options& options);
void finish(Machine& machine, const Options& options);
void printHelp();
void printRegisters(const Machine& machine);
}
