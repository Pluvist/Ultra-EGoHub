#include "KeyboardAttachmentEdge.h"
#include <iostream>
#include <stdexcept>
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){try{
    using KeyboardAttachment::Edge;
    Edge startup;
    Check(!startup.Observe(true,{},0,true),"unknown startup is quiet");
    Check(!startup.Observe(true,true,10,true),"already attached at startup is quiet");
    Check(!startup.Observe(true,true,400,true),"steady attachment cannot replay");
    Check(!startup.Observe(true,{},500,true)&&!startup.Observe(true,true,600,true),"missing data cannot manufacture an edge");
    Check(!startup.Observe(true,false,800,true)&&startup.Observe(true,true,1000,true),"known detached to attached emits once");
    Check(!startup.Observe(true,true,1400,true),"repeated snapshots remain quiet");
    Check(!startup.Observe(true,false,1500,true)&&!startup.Observe(true,true,1600,true),"contact bounce inside one second is consumed");
    Check(!startup.Observe(true,true,2100,true),"suppressed bounce is never delayed");
    Check(!startup.Observe(true,false,2300,true)&&startup.Observe(true,true,2500,true),"later genuine attachment edge can emit");
    Edge arrival;
    Check(!arrival.Observe(true,false,0,true)&&arrival.Observe(true,true,100,true),"known physical detachment establishes a baseline");
    Check(!arrival.Observe(false,false,200,true)&&!arrival.Observe(true,true,1500,true),"host recovery cannot invent an attachment edge");
    Check(!arrival.Observe(true,false,1700,true)&&arrival.Observe(true,true,1800,true),"healthy false-to-true works after recovery");
    arrival.Reset();
    Check(!arrival.Observe(true,true,3000,true),"channel reconnect rebaselines silently");
    Edge disabled;
    Check(!disabled.Observe(true,false,0,false)&&!disabled.Observe(true,true,100,false),"disabled notifications still consume baseline");
    Check(!disabled.Observe(true,true,2000,true),"enabling does not replay an old transition");
    Check(!disabled.Observe(true,false,2200,true)&&disabled.Observe(true,true,2400,true),"future enabled transition emits");
    Edge unknown;
    Check(!unknown.Observe(true,false,0,true)&&!unknown.Observe(true,{},100,true)&&unknown.Observe(true,true,200,true),"unknown snapshot between physical detach and attach preserves the edge");
    std::cout<<"Keyboard attachment startup, unknown, recovery and bounded edge checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
