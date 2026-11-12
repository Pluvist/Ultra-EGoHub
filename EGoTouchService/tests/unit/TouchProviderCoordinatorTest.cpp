#include "TouchProviderCoordinator.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace std::chrono_literals;
using Service::TouchProviderCoordinator;
using Service::TouchProviderError;
using PenStatus::TouchProviderState;
void Require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
struct Fixture {
    TouchProviderCoordinator::Clock::time_point now{1s};
    int starts=0,stops=0,disables=0,failures=0,notifications=0;
    bool alive=true,disableOk=true,stopOk=true;
    std::chrono::seconds startCost{0};
    TouchProviderCoordinator Make(){
        Service::TouchProviderOperations ops;
        ops.now=[this]{return now;};
        ops.disableHuawei=[this]{++disables;return disableOk;};
        ops.stopHuawei=[]{return true;};
        ops.stopEGo=[this]{++stops;return stopOk;};
        ops.startEGo=[this]{++starts;now+=startCost;if(failures>0){--failures;return false;}return true;};
        ops.egoAlive=[this]{return alive;};
        return {ops,[this](auto state,auto error){
            Require(state!=TouchProviderState::Huawei && state!=TouchProviderState::SwitchingToHuawei,"OEM selected");
            if(error==TouchProviderError::RetriesExhausted)++notifications;
        },5s};
    }
    void Step(TouchProviderCoordinator& c){now+=2s;c.Tick(now);}
};
void FiveRetries(){
    Fixture f;f.failures=100;auto c=f.Make();
    Require(!c.AcquireOrRenew(f.now),"initial failure");
    Require(f.starts==1 && f.notifications==0,"no early notification");
    for(int retry=1;retry<=5;++retry){
        c.Tick(f.now+1s);Require(f.starts==retry,"retry delay");
        f.Step(c);Require(f.starts==retry+1,"one start per retry");
        Require(f.notifications==(retry==5?1:0),"notify after five retries");
    }
    Require(c.Error()==TouchProviderError::RetriesExhausted,"terminal error");
    for(int i=0;i<100;++i){c.AcquireOrRenew(f.now);f.Step(c);}
    c.Release();c.AcquireOrRenew(f.now);c.OnSuspend(f.now);c.OnResume(f.now);
    Require(f.starts==6 && f.notifications==1,"renewal and power cannot reset exhausted batch");
    Require(f.disables>6,"OEM remains audited");
}
void RecoveryAndTrayIndependence(){
    Fixture f;f.failures=2;auto c=f.Make();c.AcquireOrRenew(f.now);f.Step(c);f.Step(c);
    Require(c.State()==TouchProviderState::EGoTouch && f.starts==3,"retry succeeds");
    int stops=f.stops;c.Release();f.now+=60s;c.Tick(f.now);
    Require(f.starts==3 && f.stops==stops,"tray exit keeps input running");
    c.OnSuspend(f.now);f.Step(c);
    Require(c.IsSuspended() && f.starts==3,"no startup while suspended");
    c.OnResume(f.now);
    Require(c.State()==TouchProviderState::EGoTouch && f.starts==4,"resume without tray");
    f.alive=false;f.failures=100;f.Step(c);f.alive=true;
    for(int i=0;i<5;++i)f.Step(c);
    Require(f.notifications==1 && f.starts==10,"bounded crash restart batch");
}
void SlowStart(){
    Fixture f;f.failures=100;f.startCost=12s;auto c=f.Make();c.AcquireOrRenew(f.now);
    c.Tick(f.now);c.Tick(f.now+1s);Require(f.starts==1,"anchor after slow failure");
    f.Step(c);Require(f.starts==2,"retry after delay");
}
void DisableFailure(){
    Fixture f;f.disableOk=false;auto c=f.Make();c.AcquireOrRenew(f.now);
    for(int i=0;i<5;++i)f.Step(c);
    Require(f.starts==0 && f.notifications==1,"no competing driver on disable failure");
}
void Shutdown(){
    for(bool systemShutdown:{false,true}){
        Fixture f;auto c=f.Make();c.AcquireOrRenew(f.now);c.Shutdown(systemShutdown);
        f.now+=100s;c.Tick(f.now);
        Require(f.starts==1 && f.stops==2 && f.disables==2,"shutdown keeps OEM disabled");
    }
}
int main(){try{FiveRetries();RecoveryAndTrayIndependence();SlowStart();DisableFailure();Shutdown();
    std::cout<<"Permanent-disable and retry policy tests passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
