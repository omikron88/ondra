// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from machine/Printer.java and BT100.java in omikron88/jondra,
// revision 7aad67efd76badfa6b82835bd1285aa7c9bedeb1 (GPLv3).
#include <ondra/printer.hpp>
#include <ondra/files.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ondra {
const unsigned Bt100::Width;
const unsigned Bt100::MaxRows;
Printer::Printer():enabled_(false),queued_(0),lastTime_(0),busyUntil_(0),total_(0) {}
void Printer::configure(const std::string& path) {
    if (output_.is_open()) { output_.flush(); output_.close(); }
    output_.clear(); path_=path; error_.clear(); enabled_=!path.empty(); reset(0);
}
void Printer::reset(std::uint64_t time) { queued_=0; lastTime_=time; busyUntil_=time; }
void Printer::advance(std::uint64_t time,unsigned multiplier) {
    if (time>=lastTime_) queued_=std::max(0.0,queued_-(time-lastTime_)*200.0/(2000000.0*multiplier));
    lastTime_=time;
}
void Printer::changeSpeed(std::uint64_t time,unsigned before,unsigned after) {
    advance(time,before);
    if (busyUntil_>time) busyUntil_=time+(busyUntil_-time)*after/before;
}
void Printer::strobe(std::uint8_t data,std::uint64_t time,unsigned multiplier) {
    if (!enabled_) return;
    advance(time,multiplier);
    try {
        if (!output_.is_open()) {
#ifdef _WIN32
            output_.open(utf16(path_).c_str(),std::ios::binary|std::ios::app);
#else
            output_.open(path_.c_str(),std::ios::binary|std::ios::app);
#endif
        }
        output_.put(static_cast<char>(data)); output_.flush();
        if (!output_) throw std::runtime_error("Cannot append printer output to "+path_);
        ++total_;
    } catch (const std::exception& e) {
        error_=e.what(); enabled_=false;
        return; // Host I/O failure must not unwind a partially executed Z80 instruction.
    }
    queued_+=1;
    const auto delay=static_cast<std::uint64_t>(std::max(200.0,(queued_-2048)/200*2000000)*multiplier);
    busyUntil_=time+delay;
}
void Printer::flush() {
    if (!error_.empty()) throw std::runtime_error(error_);
    if (output_.is_open()) { output_.flush(); if (!output_) throw std::runtime_error("Cannot flush printer output to "+path_); }
}

Bt100::Bt100():enabled_(false),power_(false),output_(255),x_(0),feed_(0),row_(0),lastTime_(0),moveFrom_(0) {}
void Bt100::reset(std::uint64_t time) { lastTime_=moveFrom_=time; output_=255; }
void Bt100::power(bool on,std::uint64_t time,unsigned multiplier) {
    if (on==power_) return;
    advance(time,multiplier); power_=on;
    if (on && (output_&0x84)!=0x84) moveFrom_=time+3000*multiplier;
}
void Bt100::advance(std::uint64_t time,unsigned multiplier) {
    if (time<lastTime_) lastTime_=time;
    if (!enabled_ || !power_) { lastTime_=time; return; }
    const double dt=double(time-lastTime_)/multiplier;
    const auto start=std::max(lastTime_,moveFrom_);
    const double motion=time>start?double(time-start)/multiplier:0;
    lastTime_=time;
    if (!(output_&4)) x_=std::min(526.0,x_+motion/4000);
    if (!(output_&128)) x_=std::max(-0.75,x_-motion/4000);
    if (!(output_&16)) {
        const double feed=feed_+dt/10000,rows=std::floor(feed);
        feed_=feed-rows;
        row_+=static_cast<unsigned>(std::min(rows,double(MaxRows-row_)));
    }
}
void Bt100::changeSpeed(std::uint64_t time,unsigned before,unsigned after) {
    advance(time,before);
    if (moveFrom_>time) moveFrom_=time+(moveFrom_-time)*after/before;
}
void Bt100::write(std::uint8_t value,std::uint64_t time,unsigned multiplier) {
    if (!enabled_) return;
    advance(time,multiplier);
    const auto old=output_; output_=value;
    if ((old&0x84)==0x84 && (output_&0x84)!=0x84) moveFrom_=time+3000*multiplier;
    if (power_ && (old&1) && !(output_&1)) strike();
}
void Bt100::strike() {
    const int column=static_cast<int>(std::floor(x_+((output_&128)?0:2)+0.5));
    if (column<0 || column>=int(Width) || row_>=MaxRows) return;
    if (paper_.size()<=row_) paper_.resize(row_+1,std::array<std::uint8_t,Width/8>());
    paper_[row_][column/8]|=static_cast<std::uint8_t>(128>>(column%8));
}
unsigned Bt100::sensors(std::uint64_t time,unsigned multiplier) {
    if (!enabled_) return 0;
    advance(time,multiplier); unsigned value=0;
    if (x_-std::floor(x_)<0.5) value|=2;
    const double coordinate=x_-std::floor(x_/8+0.5)*8;
    if (coordinate< -0.5 || coordinate>=0.45) value|=1;
    if (x_<=0) value|=4;
    if (!(output_&16) && feed_>=0.3 && feed_<0.7) value|=8;
    return value;
}
unsigned Bt100::sensorLines(std::uint64_t time,unsigned multiplier) {
    return power_?(sensors(time,multiplier)^0x0a)&15:15;
}
unsigned Bt100::paperRows() const { return std::min(MaxRows,std::max(row_+1,static_cast<unsigned>(paper_.size()))); }
bool Bt100::dot(unsigned row,unsigned column) const {
    return row<paper_.size() && column<Width && (paper_[row][column/8]&(128>>(column%8)));
}
void Bt100::newPaper() { paper_.clear(); row_=0; feed_=0; }

namespace {
void bigEndian(std::vector<std::uint8_t>& data,std::uint32_t value) {
    for (int shift=24;shift>=0;shift-=8) data.push_back(static_cast<std::uint8_t>(value>>shift));
}
void pngChunk(std::vector<std::uint8_t>& png,const char* type,const std::vector<std::uint8_t>& data) {
    bigEndian(png,static_cast<std::uint32_t>(data.size())); const auto start=png.size();
    png.insert(png.end(),type,type+4); png.insert(png.end(),data.begin(),data.end());
    std::uint32_t crc=0xffffffffu;
    for (std::size_t i=start;i<png.size();++i) {
        crc^=png[i];
        for (unsigned bit=0;bit<8;++bit) crc=(crc>>1)^(crc&1?0xedb88320u:0);
    }
    bigEndian(png,crc^0xffffffffu);
}
}
void Bt100::savePng(const std::string& path,unsigned scale) const {
    if (scale<1 || scale>2) throw std::runtime_error("BT100 image scale must be 1 or 2");
    const unsigned width=Width*scale,height=paperRows()*scale,stride=(width+7)/8;
    std::vector<std::uint8_t> raw((stride+1)*height,255);
    for (unsigned y=0;y<height;++y) {
        raw[y*(stride+1)]=0; // PNG filter: none; 1-bit grayscale (white=1).
        for (unsigned x=0;x<width;++x) if (dot(y/scale,x/scale)) raw[y*(stride+1)+1+x/8]&=static_cast<std::uint8_t>(~(128>>(x%8)));
    }
    std::vector<std::uint8_t> compressed={0x78,0x01}; // zlib, uncompressed DEFLATE blocks.
    for (std::size_t pos=0;pos<raw.size();) {
        const unsigned count=static_cast<unsigned>(std::min(std::size_t(65535),raw.size()-pos));
        compressed.push_back(pos+count==raw.size()?1:0);
        putWord(compressed,count); putWord(compressed,count^65535);
        compressed.insert(compressed.end(),raw.begin()+pos,raw.begin()+pos+count); pos+=count;
    }
    std::uint32_t a=1,b=0;
    for (auto value:raw) { a=(a+value)%65521; b=(b+a)%65521; }
    bigEndian(compressed,(b<<16)|a);
    std::vector<std::uint8_t> png={137,80,78,71,13,10,26,10},header;
    bigEndian(header,width); bigEndian(header,height); header.insert(header.end(),{1,0,0,0,0});
    pngChunk(png,"IHDR",header); pngChunk(png,"IDAT",compressed); pngChunk(png,"IEND",{});
    writeFile(path,png);
}
}
