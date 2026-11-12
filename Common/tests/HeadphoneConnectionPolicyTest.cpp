#include "HeadphoneConnectionPolicy.h"
#include <iostream>
#include <stdexcept>
static void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(){try{
    using HeadphoneConnections::Edges;
    using HeadphoneConnections::PendingConnections;
    using HeadphoneConnections::CacheProbeSchedule;
    using HeadphoneConnections::ReconnectSchedule;
    using HeadphoneConnections::IsHuaweiHeadphoneName;
    Check(IsHuaweiHeadphoneName(L"HUAWEI FreeBuds Pro 5"),"Huawei earbuds match");
    Check(IsHuaweiHeadphoneName(L"FreeClip 2 典藏版"),"Clip collector name matches");
    Check(IsHuaweiHeadphoneName(L"华为智能眼镜"),"Chinese glasses match");
    Check(!IsHuaweiHeadphoneName(L"HUAWEI Mouse CD28"),"Huawei mouse is not a headset");
    Check(!IsHuaweiHeadphoneName(L"Razer Barracuda X"),"other vendors do not notify");
    Check(!IsHuaweiHeadphoneName(L"My Headphones"),"unknown aliases are not guessed");
    Edges edges;
    Check(!edges.ObserveSnapshot(0x112233445566ULL,true,100,true),"already connected startup baseline is silent");
    Check(!edges.ObserveAcl(0x112233445566ULL,true,200),"duplicate connected event is silent");
    Check(!edges.ObserveAcl(0x112233445566ULL,false,300),"disconnect itself does not notify");
    Check(!edges.ObserveAcl(0x112233445566ULL,true,3299),"short radio interruption does not notify again");
    edges.ObserveAcl(0x112233445566ULL,false,4000);
    Check(edges.Deadline()==7000,"disconnect grace has a bounded wake deadline");
    edges.Advance(7000);
    Check(edges.ObserveAcl(0x112233445566ULL,true,7001),"genuine reconnect notifies once");
    Check(!edges.ObserveAcl(0x112233445566ULL,true,7002),"repeated ACL events remain deduplicated");
    Check(edges.ObserveAcl(0x102030405060ULL,true,8000),"another headset's first connection notifies");
    edges.ObserveAcl(0x102030405060ULL,false,9000);
    Check(edges.ObserveAcl(0x102030405060ULL,true,13000),"late reconnect works even without an explicit Advance call");
    Check(!edges.ObserveAcl(0,true,14000)&&!edges.ObserveAcl(0x1000000000000ULL,true,14000),"invalid addresses rejected");
    edges.Reset();Check(!edges.ObserveSnapshot(1,true,0,true),"restart establishes a new silent baseline");

    constexpr uint64_t device=0x112233445566ULL;
    Edges lag;
    Check(!lag.ObserveSnapshot(device,false,0,true),"disconnected startup baseline is silent");
    Check(lag.ObserveAcl(device,true,100),"real ACL connection notifies before cache catches up");
    Check(!lag.ObserveSnapshot(device,false,200)&&!lag.Deadline(),"stale cache false cannot start disconnect debounce");
    lag.Advance(3200);
    Check(!lag.ObserveSnapshot(device,true,3300),"cache catches up without replaying or losing ACL connection");
    Check(!lag.ObserveAcl(device,false,4000)&&lag.Deadline()==7000,"real ACL disconnect still starts three-second grace");
    Check(!lag.ObserveSnapshot(device,true,4500)&&lag.Deadline()==7000,"stale cache true cannot cancel real disconnect grace");
    lag.Advance(7000);
    Check(lag.ObserveAcl(device,true,7001),"real reconnect overrides opposite snapshot protection immediately");
    lag.ObserveAcl(device,false,8000);
    lag.ObserveSnapshot(device,true,8100);
    Check(!lag.ObserveAcl(device,true,10999)&&!lag.Deadline(),"short ACL interruption is still deduplicated with stale snapshots");

    Edges fallback;
    Check(fallback.ObserveAcl(device,true,0),"initial ACL event delivered");
    Check(!fallback.ObserveSnapshot(device,false,4999)&&!fallback.Deadline(),"opposite snapshot held for full five seconds");
    Check(!fallback.ObserveSnapshot(device,false,5000)&&fallback.Deadline()==8000,"expired protection allows fallback to recover missed disconnect");
    fallback.Advance(8000);
    Check(fallback.ObserveSnapshot(device,true,15000),"fallback can notify reconnect without a second ACL event");
    Check(!fallback.ObserveSnapshot(device,true,30000),"fallback reconnect remains deduplicated");
    fallback.ObserveAcl(device,false,31000);
    Check(!fallback.ObserveSnapshot(device,true,35999),"opposite cache remains held after disconnect grace completes");
    Check(fallback.ObserveSnapshot(device,true,36000),"expired disconnect authority allows missed reconnect recovery");

    PendingConnections pending;
    Check(!pending.Deadline(),"idle monitor has no retry wake");
    Check(pending.Add(device,0)&&pending.Deadline()==250,"unresolved ACL metadata schedules one short retry");
    Check(pending.Due(249).empty(),"metadata is not polled before retry deadline");
    const auto retry=pending.Due(250);
    Check(retry.size()==1&&retry.front()==device,"temporarily missing metadata remains available for retry");
    Edges delayed;
    delayed.ObserveSnapshot(device,false,0,true);
    Check(delayed.ObserveAcl(retry.front(),true,250),"resolved paired identity delivers delayed ACL event exactly once");
    Check(!delayed.ObserveSnapshot(device,false,300),"resolved ACL still outranks lagging fConnected false");
    pending.Remove(device);
    Check(!pending.Deadline()&&pending.Due(500).empty(),"resolved event stops high-frequency retry wakeups");
    Check(pending.Add(device,1000)&&pending.Add(device,2800),"duplicate unresolved events share one retry lifetime");
    Check(pending.Due(2999).size()==1&&pending.Due(3000).empty()&&!pending.Deadline(),"duplicate events cannot extend the two-second deadline");
    pending.Add(device,4000);pending.Remove(device);
    Check(pending.Due(4250).empty()&&!pending.Deadline(),"explicit disconnect cancels pending connection retry");
    Check(!pending.Add(0,0)&&!pending.Add(0x1000000000000ULL,0),"retry queue rejects invalid addresses");
    for(uint64_t address=1;address<=PendingConnections::kMaxEntries;++address)
        Check(pending.Add(address,0),"bounded retry queue accepts its capacity");
    Check(!pending.Add(99,0),"unrelated event bursts cannot grow retry queue without bound");
    Check(pending.Due(250).size()==PendingConnections::kMaxEntries&&pending.Due(251).empty(),"bounded batch retries never become a busy loop");
    Check(pending.Due(2000).empty()&&!pending.Deadline(),"expired metadata retries return to low-frequency fallback");

    CacheProbeSchedule probes;
    Check(!probes.Deadline()&&!probes.Due(0)&&!probes.Due(15000),"no device hint means no extra cache wakeup");
    Check(probes.Hint(1000)&&probes.Deadline()==1000,"device hint schedules immediate read-only scan");
    Check(!probes.Due(999)&&probes.Due(1000)&&probes.Deadline()==1100,"first follow-up is 100ms after the initial hint");
    Check(!probes.Hint(1050)&&probes.Deadline()==1100,"repeated device hints do not restart the burst");
    Check(probes.Due(1100)&&probes.Deadline()==1500&&!probes.Due(1499),"second follow-up is exactly 500ms after the hint");
    Check(probes.Due(1500)&&probes.Deadline()==2200,"third follow-up is exactly 1200ms after the hint");
    Check(!probes.Hint(2199)&&probes.Due(2200)&&probes.Deadline()==3500,"later hints cannot postpone the final 2500ms probe");
    Check(!probes.Hint(3499)&&probes.Due(3500)&&!probes.Deadline()&&!probes.Due(3501),"completed burst clears every extra cache deadline");
    Check(probes.Hint(4000)&&probes.Due(10000)&&!probes.Deadline(),"delayed worker coalesces elapsed probes rather than spinning");

    Edges noHci;
    noHci.ObserveSnapshot(device,false,0,true);
    probes.Hint(5000);
    Check(probes.Due(5000)&&!noHci.ObserveSnapshot(device,false,5000),"device-arrival scan tolerates stale disconnected cache");
    Check(probes.Due(5100)&&!noHci.ObserveSnapshot(device,false,5100),"early follow-up does not invent a connection");
    Check(probes.Due(5500)&&noHci.ObserveSnapshot(device,true,5500),"cache-only connection notifies promptly without an HCI event");
    Check(probes.Due(6200)&&!noHci.ObserveSnapshot(device,true,6200),"follow-up scans do not repeat a delivered connection");
    Check(probes.Due(7500)&&!noHci.ObserveSnapshot(device,true,7500)&&!probes.Deadline(),"last probe is deduplicated and restores idle cadence");

    constexpr uint64_t other=0x102030405060ULL;
    ReconnectSchedule reconnect;
    Edges live;
    const auto sync=[&](uint64_t now){
        live.Advance(now);
        if(auto target=reconnect.Target())reconnect.ObserveTarget(live.IsConnected(target),live.DisconnectPending(target),now);
    };
    reconnect.SetEnabled(true);
    live.ObserveSnapshot(device,false,0,true);sync(0);
    Check(!reconnect.Target()&&!reconnect.Deadline()&&!reconnect.TakeDue(100000),"disconnected baseline never starts automatic requests");
    live.ObserveSnapshot(device,true,0,true);reconnect.Connected(device,0,true);sync(0);
    reconnect.Connected(other,0,true);
    Check(reconnect.Target()==device&&!reconnect.Deadline(),"baseline selects one live target without issuing audio requests");
    reconnect.Connected(device,500);
    Check(!reconnect.Deadline(),"duplicate baseline ACL true never issues reconnect requests");
    live.ObserveAcl(device,false,1000);reconnect.Disconnected(device,1000);sync(1000);
    Check(live.Deadline()==4000&&reconnect.Deadline()==1250&&!reconnect.TakeDue(1249),"fast reconnect has a separate 250ms delay while notification grace stays three seconds");
    Check(reconnect.TakeDue(1250)==device&&reconnect.Deadline()==2250,"first audio request does not wait for notification debounce");
    reconnect.Disconnected(device,1300);sync(1300);
    live.ObserveSnapshot(device,true,2000);sync(2000);
    Check(reconnect.Deadline()==2250,"duplicate false and stale cache true cannot reset a disconnect round");
    Check(reconnect.TakeDue(2250)==device&&reconnect.Deadline()==4250,"second disconnect attempt is one second after round start");
    sync(4000);
    Check(reconnect.Deadline()==4250,"effective Edges disconnect does not restart the earlier ACL round");
    Check(!reconnect.TakeDue(4249)&&reconnect.TakeDue(4250)==device&&reconnect.Deadline()==8250,"third disconnect attempt uses the original three-second offset");
    Check(reconnect.TakeDue(8250)==device&&reconnect.Deadline()==16250,"fourth disconnect attempt uses the seven-second offset");
    Check(reconnect.TakeDue(16250)==device&&reconnect.Deadline()==31250,"fifth disconnect attempt uses the fifteen-second offset");
    Check(!reconnect.TakeDue(31249)&&reconnect.TakeDue(31250)==device&&!reconnect.Deadline(),"sixth disconnect attempt stops the round at thirty seconds");
    reconnect.Disconnected(device,40000);sync(40000);
    Check(!reconnect.Deadline(),"persistent disconnected state cannot restart an exhausted round");

    live.ObserveAcl(device,true,41000);reconnect.Connected(device,41000);sync(41000);
    Check(!reconnect.Deadline()&&reconnect.PresenceTarget()==0,"real ACL up stops all automatic work and passive listening");
    reconnect.Connected(device,41100);
    Check(!reconnect.Deadline()&&!reconnect.TakeDue(41149),"duplicate ACL true cannot restart automatic work");
    Check(!reconnect.TakeDue(41150)&&!reconnect.TakeDue(41800)&&!reconnect.TakeDue(43000),"connected devices receive no delayed audio profile requests");
    reconnect.Connected(device,44000);
    Check(!reconnect.Deadline(),"continuously connected ACL stays free of automatic reconnect requests");
    live.ObserveAcl(device,false,45000);reconnect.Disconnected(device,45000);sync(45000);
    Check(!live.ObserveAcl(device,true,45100),"short real ACL interruption remains silent for notifications");
    reconnect.Connected(device,45100);sync(45100);
    Check(!reconnect.Deadline()&&!reconnect.TakeDue(45250),"short real false-to-true cancels reconnect even while notification remains deduplicated");
    reconnect.CancelRound();
    Check(!reconnect.Deadline(),"terminal backend results can stop an automatic stage without changing connection evidence");

    live.ObserveAcl(device,false,46000);reconnect.Disconnected(device,46000);sync(46000);
    reconnect.SetEnabled(false);reconnect.SetEnabled(true);sync(49000);
    Check(!reconnect.Deadline(),"disable then reenable cannot resurrect an old disconnect round");
    reconnect.SetEnabled(false);
    live.ObserveAcl(device,true,50000);reconnect.Connected(device,50000);sync(50000);
    Check(!reconnect.Deadline(),"disabled mode never schedules reconnect requests");
    live.ObserveAcl(device,false,51000);reconnect.Disconnected(device,51000);sync(51000);
    reconnect.SetEnabled(true);sync(54000);
    Check(!reconnect.Deadline(),"enabling during an existing ACL disconnect cannot arm history");
    live.ObserveAcl(device,true,55000);reconnect.Connected(device,55000);sync(55000);
    live.ObserveAcl(device,false,56000);reconnect.Disconnected(device,56000);sync(56000);
    Check(reconnect.Deadline()==56250,"next real disconnect uses the enabled preference");
    live.ObserveAcl(other,true,56100);reconnect.Connected(other,56100);sync(56100);
    Check(reconnect.Target()==other&&!reconnect.Deadline(),"new connected target cancels all old target attempts without further audio requests");
    reconnect.Disconnected(device,56150);
    Check(reconnect.Target()==other&&!reconnect.Deadline(),"old target disconnect cannot mutate the current connected target");
    reconnect.ManualOverride(other,56200);
    Check(reconnect.Target()==other&&!reconnect.Deadline(),"manual request stays one shot and does not fake a new target");
    reconnect.ManualOverride(device,56300);
    reconnect.Disconnected(other,56400);
    Check(reconnect.Target()==other&&!reconnect.Deadline(),"manual selection of another device suppresses old target automatic work");

    ReconnectSchedule protectedRound;Edges protectedEdges;
    protectedEdges.ObserveAcl(device,true,0);protectedRound.Connected(device,0,true);protectedRound.SetEnabled(true);
    const auto protectedSync=[&](uint64_t now){
        protectedEdges.Advance(now);
        protectedRound.ObserveTarget(protectedEdges.IsConnected(device),protectedEdges.DisconnectPending(device),now);
    };
    protectedEdges.ObserveSnapshot(device,false,100);protectedSync(100);protectedSync(4000);
    Check(!protectedRound.Deadline(),"stale cache false within ACL authority cannot initiate reconnect");
    protectedEdges.ObserveSnapshot(device,false,5000);protectedSync(5000);protectedSync(7999);
    Check(!protectedRound.Deadline(),"cache-only fallback must wait for effective Edges disconnect");
    protectedSync(8000);
    Check(protectedRound.Deadline()==8250&&protectedRound.TakeDue(8250)==device,"effective fallback transition starts only one bounded fast round");
    protectedRound.ObserveTarget(false,false,9000);
    Check(protectedRound.Deadline()==9250,"repeated effective false cannot delay or restart fallback attempts");

    ReconnectSchedule disabledCache;Edges cacheEdges;
    cacheEdges.ObserveSnapshot(device,true,0,true);disabledCache.Connected(device,0,true);
    cacheEdges.ObserveSnapshot(device,false,100);
    disabledCache.ObserveTarget(cacheEdges.IsConnected(device),cacheEdges.DisconnectPending(device),100);
    disabledCache.SetEnabled(true);cacheEdges.Advance(3100);
    disabledCache.ObserveTarget(cacheEdges.IsConnected(device),cacheEdges.DisconnectPending(device),3100);
    Check(!disabledCache.Deadline(),"enabling cannot arm a cache disconnect already pending while disabled");

    ReconnectSchedule delayedRound;
    delayedRound.SetEnabled(true);delayedRound.Connected(device,0,true);delayedRound.Disconnected(device,0);
    Check(delayedRound.TakeDue(20000)==device&&delayedRound.Deadline()==30250&&!delayedRound.TakeDue(20000),"overdue disconnect attempts coalesce rather than issuing a burst");
    Check(delayedRound.TakeDue(30250)==device&&!delayedRound.Deadline(),"delayed round still stops at its last finite deadline");

    ReconnectSchedule restored;
    restored.RestoreDisconnected(0);restored.RestoreDisconnected(0x1000000000000ULL);
    Check(!restored.Target(),"invalid saved target cannot be restored");
    restored.RestoreDisconnected(device);restored.SetEnabled(true);
    Check(restored.Target()==device&&restored.PresenceTarget()==device&&!restored.Deadline(),"restored paired target only enables passive presence, never an immediate request");
    Check(!restored.PresenceHint(other,0),"another device cannot rearm the saved target");
    Check(restored.PresenceHint(device,10000)&&restored.Deadline()==10250,"new exact presence starts a bounded disconnect round");
    for(uint64_t now=11000;now<=42000;now+=1000){
        Check(!restored.PresenceHint(device,now),"continuous advertising never restarts or extends an active episode");
        (void)restored.TakeDue(now);
    }
    Check(!restored.Deadline()&&!restored.PresenceHint(device,43000),"continuous advertisements stay consumed after the finite round ends");
    Check(!restored.PresenceHint(device,47999),"presence silence shorter than five seconds cannot create a new episode");
    Check(restored.PresenceHint(device,52999)&&restored.Deadline()==53249,"five seconds of silence permits a later opening episode");
    restored.Connected(device,53000);
    Check(restored.PresenceTarget()==0&&!restored.Deadline(),"actual connection stops passive listening and every remaining automatic request");
    restored.SetEnabled(false);
    Check(!restored.Deadline()&&restored.PresenceTarget()==0,"disabled mode stops listening and every automatic phase");
    restored.Disconnected(device,54000);restored.SetEnabled(true);
    Check(restored.PresenceTarget()==device&&!restored.Deadline(),"reenabling disconnected target resumes passive listening without historical requests");
    restored.ManualOverride(device,55000);
    Check(!restored.PresenceHint(device,56000)&&!restored.Deadline(),"manual one-shot consumes the current presence episode without extending automatic work");

    std::cout<<"Headphone connection detection and bounded automatic reconnect policy checks passed\n";return 0;
}catch(std::exception const& e){std::cerr<<e.what()<<'\n';return 1;}}
