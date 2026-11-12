#include "HeadphoneConnectionMonitor.h"
#include "HeadphoneConnectionPolicy.h"
#include "HeadphoneAudioReconnect.h"
#include "HeadphonePresenceWatcher.h"
#include "PopupTimingTrace.h"
#include <bluetoothapis.h>
#include <bthdef.h>
#include <dbt.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstring>
#include <map>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#pragma comment(lib,"bthprops.lib")

namespace {
// Keep the GUID from bthdef.h exact, without requiring a new uuid.lib dependency.
constexpr GUID kBluetoothHciEvent{0xfc240062,0x1541,0x49be,{0xb4,0x63,0x84,0xc4,0xdc,0xd7,0xbf,0x7f}};
using Clock=std::chrono::steady_clock;
uint64_t Now(){return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count());}
struct Device {bool connected=false;};
bool Eligible(BLUETOOTH_DEVICE_INFO const& info){
    return (info.fAuthenticated||info.fRemembered)&&HeadphoneConnections::IsHuaweiHeadphoneName(info.szName);
}
uint64_t SavedTarget(){
    uint64_t address=0;DWORD size=sizeof(address);
    if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\OpenEGoHub",L"LastHeadphoneAddress",
                    RRF_RT_REG_QWORD,nullptr,&address,&size)!=ERROR_SUCCESS||size!=sizeof(address)||
       !address||address>0xffffffffffffULL)return 0;
    return address;
}
bool SaveTarget(uint64_t address){
    HKEY key=nullptr;
    if(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\OpenEGoHub",0,nullptr,0,KEY_SET_VALUE,
                       nullptr,&key,nullptr)!=ERROR_SUCCESS)return false;
    const auto result=RegSetValueExW(key,L"LastHeadphoneAddress",0,REG_QWORD,
                                   reinterpret_cast<const BYTE*>(&address),sizeof(address));
    RegCloseKey(key);return result==ERROR_SUCCESS;
}
std::optional<std::map<uint64_t,Device>> Snapshot(){
    std::map<uint64_t,Device> out;
    BLUETOOTH_DEVICE_SEARCH_PARAMS params{sizeof(params)};
    params.fReturnAuthenticated=TRUE;params.fReturnRemembered=TRUE;params.fReturnConnected=TRUE;
    params.fIssueInquiry=FALSE;
    BLUETOOTH_DEVICE_INFO info{sizeof(info)};
    auto search=BluetoothFindFirstDevice(&params,&info);
    if(!search){
        auto error=GetLastError();
        if(error==ERROR_NO_MORE_ITEMS||error==ERROR_NOT_FOUND||error==ERROR_INVALID_HANDLE)return out;
        return {};
    }
    do{
        if(Eligible(info))out[info.Address.ullLong]={info.fConnected!=FALSE};
        info={sizeof(info)};
    }while(BluetoothFindNextDevice(search,&info));
    const auto error=GetLastError();BluetoothFindDeviceClose(search);
    if(error!=ERROR_NO_MORE_ITEMS)return {};
    return out;
}
}

struct HeadphoneConnectionMonitor::Impl {
    struct Radio {HANDLE handle=nullptr;HDEVNOTIFY notification=nullptr;};
    struct Event {uint64_t address;bool connected;};
    HWND owner=nullptr;UINT message=0;
    std::mutex mutex;
    std::condition_variable changed;
    bool stopping=false,wake=false,refreshRadios=false,releaseRadios=false;
    bool requestedFastReconnect=false,fastReconnectChanged=false,cancelAutomatic=false;
    uint64_t manualRequest=0,activeRequest=0;
    uint64_t presenceAddress=0,presenceAt=0,savedTarget=0;
    std::vector<Event> events;
    std::vector<Radio> radios;
    std::vector<uint64_t> radioAddresses;
    std::thread worker;
    HeadphoneConnections::Edges edges;
    HeadphoneConnections::PendingConnections pending;
    HeadphoneConnections::CacheProbeSchedule probes;
    HeadphoneConnections::ReconnectSchedule reconnect;
    HeadphonePresenceWatcher presence;
    std::map<uint64_t,Device> last;
    void CloseRadios(){
        for(auto const& r:radios){if(r.notification)UnregisterDeviceNotification(r.notification);if(r.handle)CloseHandle(r.handle);}
        radios.clear();
    }
    void RegisterRadios(){
        CloseRadios();BLUETOOTH_FIND_RADIO_PARAMS params{sizeof(params)};HANDLE radio=nullptr;
        std::vector<uint64_t> current;
        bool validated=true;
        auto find=BluetoothFindFirstRadio(&params,&radio);
        if(!find){const auto error=GetLastError();validated=error==ERROR_NO_MORE_ITEMS||error==ERROR_NOT_FOUND;}
        if(find)do{
            BLUETOOTH_RADIO_INFO info{sizeof(info)};
            if(BluetoothGetRadioInfo(radio,&info)==ERROR_SUCCESS)current.push_back(info.address.ullLong);
            else validated=false;
            DEV_BROADCAST_HANDLE filter{};filter.dbch_size=sizeof(filter);filter.dbch_devicetype=DBT_DEVTYP_HANDLE;filter.dbch_handle=radio;
            auto notify=RegisterDeviceNotificationW(owner,&filter,DEVICE_NOTIFY_WINDOW_HANDLE);
            radios.push_back({radio,notify});radio=nullptr;
        }while(BluetoothFindNextRadio(find,&radio));
        if(find){
            if(GetLastError()!=ERROR_NO_MORE_ITEMS)validated=false;
            BluetoothFindRadioClose(find);
        }
        std::sort(current.begin(),current.end());
        if(validated&&current!=radioAddresses){radioAddresses=std::move(current);presence.NotifyRadioChanged();}
    }
    void RememberConnectedTarget(){
        const auto target=reconnect.Target();
        if(target&&target!=savedTarget&&SaveTarget(target))savedTarget=target;
    }
    void RestoreTarget(){
        if(reconnect.Target()||!savedTarget)return;
        BLUETOOTH_DEVICE_INFO info{sizeof(info)};info.Address.ullLong=savedTarget;
        if(BluetoothGetDeviceInfo(nullptr,&info)!=ERROR_SUCCESS||!Eligible(info))return;
        if(info.fConnected){
            const auto now=Now();edges.ObserveSnapshot(savedTarget,true,now,true);
            reconnect.Connected(savedTarget,now,true);
            if(edges.Contains(savedTarget))last[savedTarget]={true};
        }else reconnect.RestoreDisconnected(savedTarget);
        PopupTimingTrace::Record("monitor.restore_target",info.fConnected?1:0);
    }
    void Notify(uint64_t address,bool fromAcl){
        // The owner validates its notification preference before presenting UI.
        PopupTimingTrace::Record(fromAcl?"monitor.acl_notify":"monitor.scan_notify");
        PostMessageW(owner,message,0,static_cast<LPARAM>(address));
    }
    void Scan(bool baseline){
        auto snapshot=Snapshot();if(!snapshot)return;
        auto now=Now();
        // Retain known addresses when cache records briefly disappear. A
        // protected opposite snapshot must be reconsidered at the next scan.
        for(auto& [address,device]:last)if(!snapshot->contains(address)&&edges.Contains(address)){
            edges.ObserveSnapshot(address,false,now,baseline);device.connected=false;
        }
        for(auto const& [address,device]:*snapshot)
        {
            const bool notify=edges.ObserveSnapshot(address,device.connected,now,baseline);
            if(device.connected&&(baseline||notify)){
                reconnect.Connected(address,now,baseline);RememberConnectedTarget();
            }
            if(notify)Notify(address,false);
            if(edges.Contains(address))last[address]=device;
        }
        std::erase_if(last,[this](const auto& entry){return !edges.Contains(entry.first);});
    }
    enum class Resolution {Complete,Retry};
    Resolution ResolveConnection(uint64_t address){
        BLUETOOTH_DEVICE_INFO info{sizeof(info)};info.Address.ullLong=address;
        if(BluetoothGetDeviceInfo(nullptr,&info)!=ERROR_SUCCESS)return Resolution::Retry;
        if(!Eligible(info)){
            // Known Huawei devices may temporarily lose their name/pair flags
            // while the cache catches up. Never notify before revalidation.
            const bool incomplete=info.szName[0]==L'\0'||
                ((!info.fAuthenticated&&!info.fRemembered)&&
                 HeadphoneConnections::IsHuaweiHeadphoneName(info.szName));
            return edges.Contains(address)||last.contains(address)||incomplete?
                Resolution::Retry:Resolution::Complete;
        }
        // Actual ACL state wins over fConnected, which may still be stale.
        const auto now=Now();
        const bool notify=edges.ObserveAcl(address,true,now);
        if(edges.Contains(address)){reconnect.Connected(address,now);RememberConnectedTarget();}
        if(notify)Notify(address,true);
        if(edges.Contains(address))last[address]={true};
        return Resolution::Complete;
    }
    void Apply(Event const& event){
        if(event.connected){
            if(ResolveConnection(event.address)==Resolution::Retry)pending.Add(event.address,Now());
            else pending.Remove(event.address);
            return;
        }
        pending.Remove(event.address);
        // A real disconnect cancels unresolved connects even when radio/cache
        // records have vanished, and keeps the original three-second debounce.
        if(!last.contains(event.address)&&!edges.Contains(event.address)){
            BLUETOOTH_DEVICE_INFO info{sizeof(info)};info.Address.ullLong=event.address;
            if(BluetoothGetDeviceInfo(nullptr,&info)!=ERROR_SUCCESS||!Eligible(info))return;
        }
        const auto now=Now();
        edges.ObserveAcl(event.address,false,now);
        reconnect.Disconnected(event.address,now);
        if(edges.Contains(event.address))last[event.address]={false};
    }
    void UpdateReconnectState(uint64_t now){
        const auto target=reconnect.Target();
        if(target)reconnect.ObserveTarget(edges.IsConnected(target),edges.DisconnectPending(target),now);
    }
    void RunReconnectRequest(uint64_t address,bool manual){
        {
            std::lock_guard lock(mutex);
            if(stopping||(!manual&&(!requestedFastReconnect||manualRequest)))return;
            activeRequest=address;
        }
        PopupTimingTrace::Record("monitor.reconnect_request",manual?1:0);
        HeadphoneAudioReconnect::Result result{HeadphoneAudioReconnect::Status::Failed,E_FAIL};
        try{result=HeadphoneAudioReconnect::Request(address);}catch(...){}
        {
            std::lock_guard lock(mutex);activeRequest=0;
        }
        PopupTimingTrace::Record("monitor.reconnect_result",static_cast<int>(result.status));
        PopupTimingTrace::Record("monitor.reconnect_hresult",static_cast<int>(result.error));
        using Status=HeadphoneAudioReconnect::Status;
        if(result.status==Status::NotPaired||result.status==Status::Unsupported||result.status==Status::AlreadyConnected)
            reconnect.CancelRound();
        // Requested means accepted, not connected. Only real ACL/cache evidence
        // may change Edges, choose a connected target, or produce a notification.
    }
    void Run(){
        savedTarget=SavedTarget();
        presence.Start([this](uint64_t address){
            {
                std::lock_guard lock(mutex);
                if(stopping)return;
                presenceAddress=address;presenceAt=Now();wake=true;
            }
            changed.notify_one();
        });
        RegisterRadios();Scan(true);RestoreTarget();
        auto nextScan=Now()+15000;
        auto nextRadioRetry=Now()+15000;
        bool fastReconnect=false;
        uint64_t listeningTarget=0;
        for(;;){
            std::vector<Event> queued;bool radiosChanged=false,radiosReleased=false;
            bool optionChanged=false,requestedFast=false,cancelAuto=false;
            uint64_t manual=0;
            uint64_t present=0,presentAt=0;
            {
                std::unique_lock lock(mutex);
                uint64_t deadline=nextScan;if(auto edge=edges.Deadline())deadline=std::min(deadline,*edge);
                if(auto retry=pending.Deadline())deadline=std::min(deadline,*retry);
                if(auto probe=probes.Deadline())deadline=std::min(deadline,*probe);
                if(auto retry=reconnect.Deadline())deadline=std::min(deadline,*retry);
                changed.wait_until(lock,Clock::time_point{std::chrono::milliseconds(deadline)},[this]{return stopping||wake;});
                if(stopping)break;
                queued.swap(events);wake=false;radiosChanged=refreshRadios;refreshRadios=false;
                radiosReleased=releaseRadios;releaseRadios=false;
                optionChanged=fastReconnectChanged;fastReconnectChanged=false;requestedFast=requestedFastReconnect;
                cancelAuto=cancelAutomatic;cancelAutomatic=false;
                manual=manualRequest;manualRequest=0;
                if(manual)activeRequest=manual;
                present=presenceAddress;presentAt=presenceAt;presenceAddress=0;
            }
            if(radiosReleased||radiosChanged)probes.Hint(Now());
            if(radiosReleased){
                CloseRadios();
                if(!radioAddresses.empty()){radioAddresses.clear();presence.NotifyRadioChanged();}
            }else if(radiosChanged)RegisterRadios();
            for(auto const& event:queued)Apply(event);
            for(const auto address:pending.Due(Now()))
                if(ResolveConnection(address)==Resolution::Complete)pending.Remove(address);
            edges.Advance(Now());
            bool scan=probes.Due(Now());
            if(Now()>=nextScan){
                if(radios.empty()&&Now()>=nextRadioRetry){RegisterRadios();nextRadioRetry=Now()+15000;}
                scan=true;nextScan=Now()+(fastReconnect?2000:15000);
            }
            // Follow-up cache probes never re-register the radio handles.
            if(scan){Scan(false);RestoreTarget();}
            edges.Advance(Now());UpdateReconnectState(Now());
            // Observe queued connection evidence before enabling: historical
            // disconnects, including an existing debounce, cannot arm a round.
            if(cancelAuto)reconnect.SetEnabled(false);
            if(optionChanged){
                fastReconnect=requestedFast;reconnect.SetEnabled(fastReconnect);
                nextScan=Now()+(fastReconnect?2000:15000);
            }
            if(present){
                PopupTimingTrace::Record("monitor.presence_hint");
                if(reconnect.PresenceHint(present,presentAt))PopupTimingTrace::Record("monitor.presence_rearm");
            }
            if(manual){
                reconnect.ManualOverride(manual,Now());
            }
            const auto nextListeningTarget=reconnect.PresenceTarget();
            if(nextListeningTarget!=listeningTarget){
                listeningTarget=nextListeningTarget;presence.SetTarget(listeningTarget);
            }
            if(manual)RunReconnectRequest(manual,true);
            else if(auto address=reconnect.TakeDue(Now()))RunReconnectRequest(*address,false);
        }
        presence.Stop(); // No monitor mutex held: the callback can take it.
        CloseRadios();
    }
};

HeadphoneConnectionMonitor::HeadphoneConnectionMonitor()=default;
HeadphoneConnectionMonitor::~HeadphoneConnectionMonitor(){Stop();}
bool HeadphoneConnectionMonitor::Start(HWND owner,UINT notificationMessage){
    Stop();if(!owner||!notificationMessage)return false;
    auto impl=std::make_unique<Impl>();impl->owner=owner;impl->message=notificationMessage;
    try{impl->worker=std::thread([p=impl.get()]{p->Run();});}
    catch(...){return false;}
    m_impl=std::move(impl);return true;
}
void HeadphoneConnectionMonitor::Stop(){
    if(!m_impl)return;
    {std::lock_guard lock(m_impl->mutex);m_impl->stopping=true;}
    m_impl->changed.notify_one();if(m_impl->worker.joinable())m_impl->worker.join();m_impl.reset();
}
void HeadphoneConnectionMonitor::SetFastReconnect(bool enabled){
    if(!m_impl)return;
    {
        std::lock_guard lock(m_impl->mutex);
        if(m_impl->stopping||m_impl->requestedFastReconnect==enabled)return;
        if(!enabled)m_impl->cancelAutomatic=true;
        m_impl->requestedFastReconnect=enabled;m_impl->fastReconnectChanged=true;m_impl->wake=true;
    }
    m_impl->changed.notify_one();
}
bool HeadphoneConnectionMonitor::RequestReconnect(uint64_t address){
    if(!address||address>0xffffffffffffULL||!m_impl)return false;
    {
        std::lock_guard lock(m_impl->mutex);
        if(m_impl->stopping)return false;
        if(m_impl->activeRequest==address){m_impl->manualRequest=0;return true;}
        if(m_impl->manualRequest==address)return true;
        m_impl->manualRequest=address;m_impl->wake=true;
    }
    m_impl->changed.notify_one();return true;
}
bool HeadphoneConnectionMonitor::HandleDeviceChange(WPARAM event,LPARAM data){
    if(!m_impl)return false;
    if(event==DBT_DEVICEQUERYREMOVE||event==DBT_DEVICEREMOVEPENDING){
        if(!data||reinterpret_cast<const DEV_BROADCAST_HDR*>(data)->dbch_devicetype!=DBT_DEVTYP_HANDLE)return false;
        PopupTimingTrace::Record("monitor.device_hint",static_cast<int>(event));
        {std::lock_guard lock(m_impl->mutex);m_impl->releaseRadios=true;m_impl->wake=true;}
        m_impl->changed.notify_one();return true;
    }
    if(event==DBT_DEVICEARRIVAL||event==DBT_DEVICEREMOVECOMPLETE||event==DBT_DEVNODES_CHANGED||event==DBT_DEVICEQUERYREMOVEFAILED){
        PopupTimingTrace::Record("monitor.device_hint",static_cast<int>(event));
        {std::lock_guard lock(m_impl->mutex);m_impl->refreshRadios=true;m_impl->wake=true;}
        m_impl->changed.notify_one();return false;
    }
    if(event!=DBT_CUSTOMEVENT||!data)return false;
    auto header=reinterpret_cast<const DEV_BROADCAST_HDR*>(data);
    if(header->dbch_devicetype!=DBT_DEVTYP_HANDLE||header->dbch_size<offsetof(DEV_BROADCAST_HANDLE,dbch_data)+sizeof(BTH_HCI_EVENT_INFO))return false;
    auto notice=reinterpret_cast<const DEV_BROADCAST_HANDLE*>(data);
    if(!IsEqualGUID(notice->dbch_eventguid,kBluetoothHciEvent))return false;
    BTH_HCI_EVENT_INFO info{};std::memcpy(&info,notice->dbch_data,sizeof(info));
    if(info.connectionType!=HCI_CONNECTION_TYPE_ACL||info.bthAddress==0||info.bthAddress>0xffffffffffffULL)return false;
    PopupTimingTrace::Record("monitor.acl",info.connected?1:0);
    {std::lock_guard lock(m_impl->mutex);
        if(m_impl->events.size()>=64)m_impl->events.erase(m_impl->events.begin());
        m_impl->events.push_back({info.bthAddress,info.connected!=0});m_impl->wake=true;
    }
    m_impl->changed.notify_one();return true;
}
