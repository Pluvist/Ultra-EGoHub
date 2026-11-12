#include "PenHoverPolicy.h"
#include "PenHoverChannel.h"
#include <iostream>
#include <stdexcept>
#include <string>

void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){try{
    std::array<uint8_t,17> report{8,0x22,0,0x12,0x34,0x56,0x78,0x9a,0x0b,1,2,3,4};
    const auto original=report;
    PenHoverPolicy::Apply(report,true,false);
    Require(report==original,"enabled hover is unchanged");
    PenHoverPolicy::Apply(report,false,false);
    Require(report[0]==8,"neutral report keeps ID");
    for(size_t i=1;i<report.size();++i)Require(report[i]==0,"disabled hover is tip-up/out-of-range, no coordinates or pressure");
    report=original;report[1]=0x21;auto ink=report;
    PenHoverPolicy::Apply(report,false,true);
    Require(report==ink,"contact pressure, coordinates and tilt remain intact");
    report=original;report[1]=0x2c;auto eraser=report;
    PenHoverPolicy::Apply(report,false,true);
    Require(report==eraser,"eraser contact survives without Tip Switch bit");
    PenHoverPolicy::Apply(report,false,false);
    Require(report[1]==0 && report[7]==0,"eraser hover is suppressed after transforms");

    const auto name=L"Local\\OpenEGoHubPenHoverTest_"+std::to_wstring(GetCurrentProcessId());
    PenHoverChannel::Reader reader;
    Require(!reader.Read(name.c_str()).has_value(),"reader does not create settings");
    PenHoverChannel::Writer writer;
    Require(writer.Open(true,name.c_str()),"writer opens");
    Require(reader.Read(name.c_str())==true,"default enabled");
    writer.Set(false);
    Require(reader.Read(name.c_str())==false,"live disable roundtrip");
    writer.Close();
    Require(writer.Open(false,name.c_str()),"writer restart while reader retains mapping");
    Require(reader.Read(name.c_str())==false,"saved off state survives writer restart");
    writer.Set(true);
    Require(reader.Read(name.c_str())==true,"live re-enable roundtrip");
    std::cout<<"Pen hover contact/filter and driver channel tests passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
