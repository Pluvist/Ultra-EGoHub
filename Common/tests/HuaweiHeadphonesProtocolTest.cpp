#include "HuaweiHeadphonesProtocol.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace HuaweiHeadphones;
static void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static Bytes Hex(std::string_view s){Bytes b;for(size_t i=0;i<s.size();i+=2)b.push_back(uint8_t(std::stoi(std::string(s.substr(i,2)),nullptr,16)));return b;}
static Frame Decode(const Bytes& b){Decoder d;auto f=d.Feed(b);Check(f.size()==1,"decode one complete frame");return f[0];}
int main(){try{
    Check(Crc(Bytes{'1','2','3','4','5','6','7','8','9'})==0x31c3,"CRC XMODEM check vector");
    Check(BatteryQuery()==Hex("5A0009000108010002000300FBB9"),"captured battery query bytes");
    Check(IdentityQuery()==Hex("5A00210001070100020003000400050006000700080009000A000B000C000F0018001900DFF3"),"captured identity query bytes");
    Check(WriteControl(Model::Buds6i,"noise",0)==Hex("5A0007002B0401020000D22D"),"ANC off capture");
    Check(WriteControl(Model::Buds6i,"anc_level",3)==Hex("5A0007002B0401020103D17F"),"6i adaptive mapping");
    Check(WriteControl(Model::Pro3,"anc_level",1)==Hex("5A0007002B0401020101F13D"),"Pro3 adaptive mapping differs");
    Check(WriteControl(Model::Buds3,"noise",1)==Hex("5A0006002B040101017800"),"legacy one-byte ANC frame");
    Check(WriteControl(Model::Pro5,"latency",1)==Hex("5A0006002B6C010101A411"),"low latency capture");
    Check(!WriteControl(Model::Unknown,"latency",1),"unknown models cannot receive writes");
    Check(!WriteControl(Model::Clip,"noise",1),"open-ear device cannot receive ANC");
    Check(!WriteControl(Model::Buds6,"dual",1),"unverified 6 dual-device setter absent");
    Check(!WriteControl(Model::Se4Anc,"wear",1)&&!WriteControl(Model::Buds6i,"quality",1),"shared command bytes do not imply support on unverified models");
    Check(!WriteControl(Model::Pro4,"noise",2),"Pro4 cannot receive unverified transparency");
    Check(WriteControl(Model::Pro5,"swipe_volume",0)==Hex("5A0009002B1E0101000202009D9B"),"exact captured legacy swipe-volume setter");
    Check(ModelFromName(L"HUAWEI FreeBuds SE 5")==Model::Unknown,"SE5 must not be confused with 5i or SE4 ANC");
    Check(ModelFromName(L"HUAWEI FreeBuds Pro 5")==Model::Pro5,"exact model-name match");
    auto valid=Packet(1,8,{{2,{80,45,100}},{3,{0,1,0}}});
    for(size_t split=0;split<=valid.size();++split){Decoder d;auto a=d.Feed(std::span(valid).first(split));auto b=d.Feed(std::span(valid).subspan(split));
        Check(a.size()+b.size()==1,"RFCOMM fragmentation at every boundary");}
    Bytes joined{1,2,3};joined.insert(joined.end(),valid.begin(),valid.end());joined.insert(joined.end(),valid.begin(),valid.end());
    Decoder stream;Check(stream.Feed(joined).size()==2,"coalesced frames and garbage prefix");
    auto damaged=valid;damaged[7]^=1;Check(Decoder().Feed(damaged).empty(),"bad CRC rejected");
    auto dup=Packet(1,8,{{2,{10,20}},{2,{30,40}}});Check(Decoder().Feed(dup).empty(),"duplicate field rejected");
    auto battery=ReadBattery(Decode(valid));Check(battery&&(*battery)[0].level==80&&(*battery)[1].charging,"battery fields and charging");
    Check(!ReadBattery(Decode(Packet(1,8,{{2,{255,200}}}))),"unavailable levels never clamped to fake percentages");
    auto identity=Packet(1,7,{{2,{1,0x6d}},{0xa,Bytes{'T','V','E','-','0','0','0','1','6','D'}},{0xf,Bytes{'T','V','E'}},{0x19,{0x17}}});
    auto id=ReadIdentity(Decode(identity));Check(id&&id->model==Model::Pro5&&id->resourceId=="00016D"&&id->colorId=="17","strict identity with matching model prefix/code");
    auto badId=Decode(identity);badId.fields[0].value={1,0x49};Check(!ReadIdentity(badId),"contradictory identity rejected");
    ConnectionReplyProgress progress;
    const std::array<Frame,1> identityOnly{Decode(identity)},batteryOnly{Decode(valid)};
    const std::array<Frame,2> incomplete{badId,Decode(Packet(1,8,{{2,{255,200}}}))};
    Check(!progress.Observe(incomplete),"invalid identity and unavailable battery cannot complete popup read");
    Check(!progress.Observe(std::array<Frame,1>{Decode(Packet(1,7,{{1,{0}}}))}),"ACK cannot complete popup read");
    Check(!progress.Observe(identityOnly),"identity alone keeps waiting for battery");
    Check(progress.Observe(batteryOnly),"validated identity and battery finish without a quiet delay");
    ConnectionReplyProgress reversed;
    Check(!reversed.Observe(batteryOnly)&&reversed.Observe(identityOnly),"reply completion is order independent");
    Check(!ConnectionReplyProgress{}.Observe(batteryOnly),"a fresh transaction cannot reuse old identity");
    for(size_t split=0;split<=identity.size();++split){
        Decoder decoder;ConnectionReplyProgress fragmented;
        auto first=decoder.Feed(std::span(identity).first(split));
        Check(!fragmented.Observe(first),"fragmented identity never substitutes for battery");
        auto second=decoder.Feed(std::span(identity).subspan(split));
        Check(!fragmented.Observe(second)&&fragmented.Observe(batteryOnly),"fragmented identity completes only with valid battery");
    }
    auto controls=ControlProfile(Model::Pro5);auto head=std::find_if(controls.begin(),controls.end(),[](auto const& c){return c.id=="head";});
    Check(head!=controls.end(),"head-motion profile exists");
    Check(!head->Parse(Decode(Packet(0x2b,0xb4,{{1,{2}},{2,{1}}}))),"other feature cannot satisfy readback");
    Check(head->Parse(Decode(Packet(0x2b,0xb4,{{1,{0xb}},{2,{1}}})))==1,"matching feature confirms state");
    Check(!head->Parse(Decode(Packet(0x2b,0xb4,{{1,{0xb}}}))),"write ACK is not a setting state");
    std::array<int,10> gains{-60,-30,-10,0,1,10,20,30,40,60};
    Check(CustomEqualizer(Model::Buds6i,100,"Hub",gains).has_value(),"bounded custom EQ");
    auto invalid=gains;invalid[2]=61;Check(!CustomEqualizer(Model::Buds6i,100,"Hub",invalid),"reject excessive EQ gain");
    Check(!CustomEqualizer(Model::Pro4,100,"Hub",gains),"unsupported EQ cannot be sent");
    Check(!CustomEqualizer(Model::Buds6i,99,"Hub",gains),"reject invalid preset slot");
    Check(PairedPeersQuery(Model::Buds7i)==Hex("5A0008002B3101000D0100AEEE"),"captured dual-device list query");
    std::array<uint8_t,6> peerAddress{1,2,3,4,5,6};
    Check(ForgetPeer(Model::Buds7i,peerAddress).has_value()&&!ForgetPeer(Model::Pro4,peerAddress),"paired-device writes isolated by model");
    Check(!ReadPairedPeer(Decode(Packet(0x2b,0x31,{{4,{1,2,3,4,5,6}}}))),"remove ACK cannot create a fake paired-device record");
    Check(ReadPairedPeer(Decode(Packet(0x2b,0x31,{{4,{1,2,3,4,5,6}},{5,{1}},{9,Bytes{'P','C'}}})))->connected,"paired device connection state");
    for(int i=int(Model::Unknown);i<=int(Model::Eyewear2);++i){
        auto model=Model(i);for(auto const& c:ControlProfile(model))for(auto const& option:c.choices){
            if(c.id!="swipe_volume")Check(Decoder().Feed(option.write).size()==1,"every TLV profile writer produces a valid unique-TLV frame");
            else Check(option.write.size()==14&&Crc(std::span(option.write).first(12))==uint16_t((option.write[12]<<8)|option.write[13]),"bounded captured legacy setter envelope");
            Check(WriteControl(model,c.id,option.value)==option.write,"model whitelist selects the same packet");
        }
    }
    std::cout<<"Huawei AAM framing, captured packets, model isolation, readback and EQ checks passed\n";
    return 0;
}catch(std::exception const& e){std::cerr<<e.what()<<'\n';return 1;}}
