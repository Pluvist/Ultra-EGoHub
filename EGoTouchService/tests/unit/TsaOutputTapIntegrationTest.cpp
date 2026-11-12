#include "TsaCore.h"
#include "TouchTrace.h"
#include "TsaOutputMapper.h"
#include "App/TsaOut.h"
#include "App/TsaStatic.h"
#include "Alg/Touch/TouchData.h"
#include "VhfTouchState.h"
#include "Rejection/GripFilter/GripFilterInternal.h"
#include <iostream>
#include <stdexcept>

static unsigned tracedDowns=0;
static void TraceSink(const Solvers::Tsa::Core::TouchTrace::Event& e){
    if(e.stage==Solvers::Tsa::Core::TouchTrace::Stage::AfterOutput && e.event==2)++tracedDowns;
}
static void Check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
int main(){try{
    using namespace Solvers::Tsa;
    static constexpr uint16_t freqs[]{75,113}, rates[]{120};
    Core::AfeCaps caps{};
    caps.numCol=60; caps.numRow=40; caps.numScanFreq=2; caps.scanFreq=freqs;
    caps.numScanRate=1; caps.scanRate=rates;
    Check(Core::Init("W273AS2700",caps),"initialize real TSACore without devices");
    Core::SetResolution(2560,1600,10);
    Core::SetFeatures(0);
    Core::Static().xMin=0; Core::Static().xMax=2560;
    Core::TouchTrace::SetSink(TraceSink);
    TsaOutputMapper mapper;
    Solvers::FixedVector<Solvers::TouchContact,20> contacts;
    VhfTouchState sent;
    auto& table=Core::Touches();
    auto seed=[&](int slot,int id,uint32_t event,bool report){
        auto& u=table.unit[slot];u={};u.id=static_cast<uint8_t>(id);u.event=event;
        u.persistFlags=report?8u:0u;u.rptPos={1000,1000};
    };
    table.count=1;table.liftOffCount=0;seed(0,1,Core::TouchEvent::kTouchDown,true);
    Core::Out::PostProcess();mapper.Collect(contacts,true);
    sent.Observe(VhfTouchPacket::Build(contacts.span(),false),true);
    int previous=1;
    for(int tap=0;tap<100;++tap){
        const int next=previous==1?2:1;
        table.count=2;table.liftOffCount=1;
        seed(0,previous,Core::TouchEvent::kLiftOff,false);
        seed(1,next,Core::TouchEvent::kTouchDown,true);
        Core::Out::PostProcess();
        Check(Core::TouchCount()==1,"old lift must not erase a new tap in the same real output frame");
        Check(Core::TouchAt(0).id==next-1 && Core::TouchAt(0).down,"new tap survives real TSACore output pruning");
        mapper.Collect(contacts,true);
        bool down=false,up=false;
        for(const auto& c:contacts){
            down|=c.id==next && c.reportEvent==Solvers::TouchReportDown;
            up|=c.id==previous && c.reportEvent==Solvers::TouchReportUp;
        }
        Check(down && up,"rapid tap produces the preceding up and the next down");
        sent.Observe(VhfTouchPacket::Build(contacts.span(),false),true);
        const auto release=sent.ReleasePackets();
        Check(release[0].bytes[31]==1 && release[0].bytes[2]==next,"only the new tap remains held in HID state");
        previous=next;
    }
    Check(tracedDowns==101,"trace sees each real output down without suppressing it");
    Core::TouchTrace::SetSink(nullptr);
    // Lifting another finger must also preserve an already-moving contact.
    table.count=2;table.liftOffCount=1;
    seed(0,previous==1?2:1,Core::TouchEvent::kLiftOff,false);
    seed(1,previous,Core::TouchEvent::kMove,true);
    Core::Out::PostProcess();
    Check(Core::TouchCount()==1 && Core::TouchAt(0).down,"one lift preserves another finger's move");
    mapper.Collect(contacts,true);
    Check(contacts.size()==1 && contacts[0].reportEvent==Solvers::TouchReportMove,"ongoing drag isn't restarted by another lift");
    // Reproduce small, quiet short taps held in Windows grip TBD for 50 ms.
    Core::SetFeatures(0x1000000u);
    auto& context=Core::GripFilter::Ctx();context.cur=0;context.prev=0;context.quality=Core::GripFilter::kQualityNone;
    Core::Static().noiseLevel=0;
    auto& finger=table.unit[0];finger={};finger.event=Core::TouchEvent::kMove;finger.majorMm=9;finger.maxMajorMm=10;
    Check(Core::GripFilter::GetDelayTime()==8,"quiet confirmed small finger retains the proven 8 ms settling threshold");
    finger.event=Core::TouchEvent::kTouchDown;finger.durationMs=0;
    Check(Core::GripFilter::GetDelayTime()==8,"confirmed first-frame down retains the short settling threshold");
    finger.majorMm=25;finger.maxMajorMm=25;
    Check(Core::GripFilter::GetDelayTime()==50,"large contacts keep the original grip delay");
    finger.majorMm=9;finger.maxMajorMm=10;finger.gripFlags=Core::GripFilter::kGripPalmShape;
    Check(Core::GripFilter::GetDelayTime()==50,"palm-like contacts retain protection");
    finger.gripFlags=0;finger.exceptionType=0x80;
    Check(Core::GripFilter::GetDelayTime()==50,"exceptional contacts retain protection");
    finger.exceptionType=0;Core::Static().noiseLevel=1;
    Check(Core::GripFilter::GetDelayTime()==50,"noisy contacts retain protection");
    Core::Static().noiseLevel=0;context.quality=Core::GripFilter::kQualityTempGrip;
    Check(Core::GripFilter::GetDelayTime()==50,"suspected grips retain protection");
    context.quality=Core::GripFilter::kQualityNone;finger.event=Core::TouchEvent::kTouchDownDebounce;
    Check(Core::GripFilter::GetDelayTime()==50,"initial unconfirmed debounce is not bypassed");
    std::cout<<"Real-core rapid taps, drag continuity and protected short-tap admission passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
