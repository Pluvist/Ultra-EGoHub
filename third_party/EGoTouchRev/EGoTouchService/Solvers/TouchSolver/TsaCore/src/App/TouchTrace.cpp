#include "../TouchTrace.h"
#include "Alg/Touch/TouchData.h"
#include <array>
namespace Solvers::Tsa::Core::TouchTrace {
namespace {
Sink g_sink=nullptr;
struct State { bool used=false; Event value{}; };
std::array<std::array<State,256>,4> g_previous{};
bool Changed(const Event& a,const Event& b){
    return a.event!=b.event || ((a.persist^b.persist)&8)!=0 || a.grip!=b.grip ||
        a.suppress!=b.suppress || a.exception!=b.exception ||
        ((a.flags^b.flags)&0x8000000u)!=0 || ((a.flags2^b.flags2)&0x4000u)!=0;
}
}
void SetSink(Sink sink){g_previous={};g_sink=sink;}
void Table(Stage stage){
    const auto sink=g_sink;if(!sink)return;
    std::array<bool,256> seen{};
    auto& prior=g_previous[static_cast<size_t>(stage)];
    for(uint8_t i=0;i<Touches().count && i<kMaxTouchNum;++i){
        const auto& u=Touches().unit[i];if(!u.id)continue;
        seen[u.id]=true;
        const Event e{stage,u.id,u.event,u.persistFlags,u.volatileFlags,u.volatileFlags2,
            u.suppressFlags,static_cast<uint32_t>(u.exceptionType),u.gripStage,u.age,u.majorMm};
        auto& p=prior[u.id];if(!p.used || Changed(e,p.value))sink(e);
        p={true,e};
    }
    for(size_t id=1;id<prior.size();++id){
        if(prior[id].used && !seen[id]){sink(Event{stage,static_cast<uint8_t>(id)});prior[id]={};}
    }
}
void Hid(uint8_t id,bool down){
    const auto sink=g_sink;if(!sink)return;
    auto& p=g_previous[static_cast<size_t>(Stage::Hid)][id];
    const Event e{Stage::Hid,id,down?2u:32u};
    if((!p.used && down) || (p.used && p.value.event!=e.event))sink(e);
    p={true,e};
}
}
