#include "HimaxAfeAdapter.h"
#include <array>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace Solvers::Tsa;
using Bytes=std::array<uint8_t,Frame::kTotalFrameSize>;
static void word(Bytes& raw,size_t offset,uint16_t v){raw[offset]=uint8_t(v);raw[offset+1]=uint8_t(v>>8);}
static void checksum(Bytes& raw,size_t start,size_t size){
    word(raw,start+5,0);uint32_t sum=0;
    for(size_t k=5;k+1<size;k+=2)sum+=raw[start+k]|(raw[start+k+1]<<8);
    word(raw,start+5,uint16_t(0u-sum));
}
static void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
static uint32_t randomWord(){static uint32_t s=0x711c7a21; s^=s<<13;s^=s>>17;s^=s<<5;return s;}
static Bytes makeFrame(int frame,bool noisy=false){
    Bytes raw{};
    for(int k=0;k<Frame::kMatrixCells;++k)word(raw,Frame::kMatrixOffset+2*k,uint16_t(32766+int(randomWord()%2001)-1000));
    word(raw,Frame::kMasterSuffixOffset+20,uint16_t(frame&1));
    word(raw,Frame::kMasterSuffixOffset+48,noisy?3000:0);
    for(int b=0;b<2;++b){
        size_t base=Frame::kSlaveHeaderOffset+Frame::kHeaderBytes+2*b*Frame::kBlockWords;
        const int mode=frame%13;
        word(raw,base,mode==0?255:mode==1?uint16_t(int(randomWord()%256)-128):uint16_t(randomWord()%40));
        word(raw,base+2,mode==0?255:mode==1?uint16_t(int(randomWord()%256)-128):uint16_t(randomWord()%60));
        for(int k=0;k<81;++k)word(raw,base+4+2*k,uint16_t(randomWord()));
    }
    checksum(raw,0,Frame::kMasterFrameSize);
    checksum(raw,Frame::kSlaveHeaderOffset,Frame::kSlaveFrameSize);
    return raw;
}

// Independent dense oracle: construct an entirely zeroed full-screen grid on
// each active report. It has no dirty-range optimization or temporal grid cache.
class DenseReference {
    std::array<uint16_t,Frame::kMatrixCells*2> grid{};
    uint16_t noise=0;
    bool connected=false;
    static uint16_t load(const uint8_t* p){return uint16_t(p[0]|(p[1]<<8));}
    static bool valid(const uint8_t* p,size_t size){
        uint32_t sum=0;for(size_t k=5;k+1<size;k+=2)sum+=load(p+k);
        return sum!=0&&(sum&0xffffu)==0;
    }
public:
    void Reset(){noise=0;grid.fill(0);}
    void SetStylusConnected(bool v){connected=v;}
    void Build(const uint8_t* p,size_t size){
        if(p&&size>=Frame::kMasterFrameSize&&valid(p,Frame::kMasterFrameSize))
            noise=load(p+Frame::kMasterSuffixOffset+48);
    }
    AfeStylusInfo BuildStylus(const uint8_t* p,size_t size){
        AfeStylusInfo out;
        if(!p||size<Frame::kTotalFrameSize)return out;
        const auto* slave=p+Frame::kSlaveHeaderOffset;
        if(!valid(slave,Frame::kSlaveFrameSize)||!connected||noise>=3000)return out;
        const auto* first=slave+Frame::kHeaderBytes;
        if(load(first)==255&&load(first+2)==255)return out;
        grid.fill(0);
        for(int b=0;b<2;++b){
            const auto* block=first+b*Frame::kBlockWords*2;
            if(load(block)==255&&load(block+2)==255)continue;
            const int centerRow=static_cast<int8_t>(load(block)&255);
            const int centerCol=static_cast<int8_t>(load(block+2)&255);
            for(int y=0;y<9;++y)for(int x=0;x<9;++x){
                const int row=centerRow-4+y,col=centerCol-4+x,index=row*60+col;
                // Preserve upstream wire behavior at malformed boundary anchors.
                if(index<0||index>=2400||col>60||row>40)continue;
                grid[b*2400+2399-index]=load(block+4+2*(y*9+x));
            }
        }
        out.status=StylusStatus::kActive;out.tx1=grid.data();out.tx2=grid.data()+2400;
        return out;
    }
};
static void compare(){
    HimaxAfeAdapter current;DenseReference reference;
    for(int f=0;f<16000;++f){
        if(f%97==0){current.Reset();reference.Reset();}
        bool connected=f%17!=0;current.SetStylusConnected(connected);reference.SetStylusConnected(connected);
        bool charging=f%37<15;current.SetChargerPlugged(charging);
        auto raw=makeFrame(f,f%19==0);
        if(f%23==0)raw[Frame::kSlaveHeaderOffset+9]^=1;
        if(f%29==0)raw[11]^=1;
        size_t len=f%31==0?100:raw.size(); const uint8_t* ptr=f%43==0?nullptr:raw.data();
        current.Build(ptr,len);reference.Build(ptr,len);
        auto x=current.BuildStylus(ptr,len);auto y=reference.BuildStylus(ptr,len);
        check(x.status==y.status&&bool(x.tx1)==bool(y.tx1)&&bool(x.tx2)==bool(y.tx2),"stylus status mismatch");
        if(x.tx1)check(std::memcmp(x.tx1,y.tx1,Frame::kMatrixCells*2)==0,"stylus first grid mismatch");
        if(x.tx2)check(std::memcmp(x.tx2,y.tx2,Frame::kMatrixCells*2)==0,"stylus second grid mismatch");
    }
    std::cout<<"16000 mixed frames: stylus status and every returned grid cell match dense reference\n";
}
int main(){try{compare();return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
