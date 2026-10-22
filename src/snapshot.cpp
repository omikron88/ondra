// SPDX-License-Identifier: GPL-3.0-or-later
// JOndra OSN v1/v2 byte layout (little endian), independent of the host ABI.
#include <ondra/machine.hpp>
#include <ondra/files.hpp>
#include <algorithm>
#include <stdexcept>

namespace ondra {
void Machine::saveSnapshot(const std::string& path) const {
    if (cpu_.resume == Z80_RESUME_XY || cpu_.resume == Z80_RESUME_IM0_XY)
        throw std::runtime_error("Finish the prefixed instruction before saving an OSN snapshot");
    const Z80& c = cpu_;
    std::vector<std::uint8_t> data = {'O', 'S', 'N', 2, c.i};
    putWord(data, Z80_HL_(c)); putWord(data, Z80_DE_(c)); putWord(data, Z80_BC_(c)); putWord(data, Z80_AF_(c));
    putWord(data, Z80_HL(c)); putWord(data, Z80_DE(c)); putWord(data, Z80_BC(c));
    putWord(data, Z80_IY(c)); putWord(data, Z80_IX(c));
    data.push_back((c.iff1 ? 1 : 0) | (c.iff2 ? 2 : 0) | (c.data.uint8_array[0] == 0xfb ? 4 : 0) |
        ((c.request & Z80_REQUEST_NMI) ? 8 : 0) | (c.int_line ? 16 : 0) | (c.resume == Z80_RESUME_HALT ? 32 : 0));
    data.push_back(c.r); data.push_back(Z80_F(c)); data.push_back(Z80_A(c));
    putWord(data, Z80_SP(c)); putWord(data, Z80_PC(c)); data.push_back(c.im); putWord(data, Z80_MEMPTR(c));
    data.push_back(a0_); data.push_back(a1_); data.push_back(a3_);
    data.push_back(static_cast<std::uint8_t>(romType_)); data.push_back(static_cast<std::uint8_t>(resolution_));
    if (romType_ == Rom::Custom) data.insert(data.end(), custom_.begin(), custom_.end());
    data.insert(data.end(), ram_.begin(), ram_.end());
    writeFile(path, data);
}
void Machine::loadSnapshot(const std::string& path) {
    const auto data = readFile(path, 65536 + 16384 + 39);
    if (data.size() < 38 || data[0] != 'O' || data[1] != 'S' || data[2] != 'N' || (data[3] != 1 && data[3] != 2))
        throw std::runtime_error("Invalid OSN v1/v2 snapshot header");
    const unsigned header = data[3] == 2 ? 39 : 38;
    const unsigned type = data[37];
    if ((type > 3 && type != 100) || data[31] > 2 || data.size() != header + 65536u + (type == 100 ? 16384u : 0u))
        throw std::runtime_error("Invalid or incomplete OSN snapshot");
    Z80 c = cpu_;
    z80_instant_reset(&c);
    c.i = data[4];
    Z80_HL_(c) = word(data, 5); Z80_DE_(c) = word(data, 7); Z80_BC_(c) = word(data, 9); Z80_AF_(c) = word(data, 11);
    Z80_HL(c) = word(data, 13); Z80_DE(c) = word(data, 15); Z80_BC(c) = word(data, 17);
    Z80_IY(c) = word(data, 19); Z80_IX(c) = word(data, 21);
    c.iff1 = (data[23] & 1) != 0; c.iff2 = (data[23] & 2) != 0;
    c.data.uint8_array[0] = (data[23] & 4) ? 0xfb : 0;
    c.request = (data[23] & 8) ? Z80_REQUEST_NMI : 0;
    c.resume = (data[23] & 32) ? Z80_RESUME_HALT : 0; c.halt_line = (data[23] & 32) != 0;
    c.r = data[24]; Z80_F(c) = data[25]; Z80_A(c) = data[26];
    Z80_SP(c) = word(data, 27); Z80_PC(c) = word(data, 29); c.im = data[31]; Z80_MEMPTR(c) = word(data, 32);
    z80_int(&c, (data[23] & 16) != 0);
    std::size_t p = header;
    if (type == 100) { std::copy(data.begin() + p, data.begin() + p + 16384, custom_.begin()); p += 16384; }
    romType_ = static_cast<Rom>(type); installRom(romType_);
    std::copy(data.begin() + p, data.end(), ram_.begin());
    cpu_ = c;
    cycles_ = frameStart_ = 0; frameActive_ = false; cycleDebt_ = 0;
    resolution_ = data[3] == 2 ? data[38] : 255;
    a0_ = data[34]; a1_ = data[35]; a3_ = data[36];
    releaseKeys(); sound_.reset(); sound_.speaker(a0_ >> 5);
    tape_.setMotor((a0_ & 16) != 0); tape_.recordLevel((a3_ & 8) != 0); sd_.reset(0);
    printer_.reset(0); bt100_.reset(0);
}
}
