#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2bth.h>
#include <windows.h>
#include <bluetoothapis.h>
#include "HeadphoneClient.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <set>

namespace HuaweiHeadphones {
namespace {
using Clock=std::chrono::steady_clock;
struct Winsock {
    bool ok=false;Winsock(){WSADATA d{};ok=WSAStartup(MAKEWORD(2,2),&d)==0;}~Winsock(){if(ok)WSACleanup();}
};
Winsock& Network(){static Winsock n;return n;}
const GUID Spp{0x1101,0,0x1000,{0x80,0,0,0x80,0x5f,0x9b,0x34,0xfb}};
bool AudioService(const GUID& g){
    if(g.Data2!=0||g.Data3!=0x1000)return false;
    static constexpr uint8_t tail[]{0x80,0,0,0x80,0x5f,0x9b,0x34,0xfb};
    if(!std::equal(std::begin(tail),std::end(tail),g.Data4))return false;
    switch(g.Data1){case 0x1108:case 0x110a:case 0x110b:case 0x110c:case 0x110d:case 0x110e:
    case 0x110f:case 0x1112:case 0x111e:case 0x111f:return true;default:return false;}
}
BLUETOOTH_DEVICE_INFO Info(uint64_t address){BLUETOOTH_DEVICE_INFO i{sizeof(i)};i.Address.ullLong=address;return i;}
}
struct Client::Impl {
    std::mutex operation,socketLock;
    std::atomic<uint64_t> generation{0};
    SOCKET socket=INVALID_SOCKET;
    Snapshot state;
    Decoder decoder;
    bool settingsInitialized=false;
    bool identityReadPending=false;
    uint64_t token=0,sessionGeneration=0;
    bool Cancelled() const{return token!=generation.load();}
    void Close(){std::lock_guard lk(socketLock);if(socket!=INVALID_SOCKET){closesocket(socket);socket=INVALID_SOCKET;}decoder.Reset();settingsInitialized=false;identityReadPending=false;}
    bool Wait(SOCKET s,bool writing,Clock::time_point deadline){
        while(!Cancelled()&&Clock::now()<deadline){
            fd_set ready,error;FD_ZERO(&ready);FD_ZERO(&error);FD_SET(s,&ready);FD_SET(s,&error);
            timeval t{0,50000};int n=select(0,writing?nullptr:&ready,writing?&ready:nullptr,&error,&t);
            if(n==SOCKET_ERROR||FD_ISSET(s,&error))return false;
            if(n>0)return true;
        }
        return false;
    }
    bool Open(uint64_t address,ConnectionOptions options={}){
        Close();if(!Network().ok)return false;
        std::vector<GUID> services{Spp};auto info=Info(address);DWORD count=0;
        if(BluetoothGetDeviceInfo(nullptr,&info)!=ERROR_SUCCESS||(!info.fAuthenticated&&!info.fRemembered))return false;
        if(!options.standardSppOnly){
            auto result=BluetoothEnumerateInstalledServices(nullptr,&info,&count,nullptr);
            if((result==ERROR_MORE_DATA||result==ERROR_SUCCESS)&&count>0&&count<=64){
                std::vector<GUID> found(count);
                if(BluetoothEnumerateInstalledServices(nullptr,&info,&count,found.data())==ERROR_SUCCESS)
                    for(auto const& g:found)if(!AudioService(g)&&!IsEqualGUID(g,Spp))services.push_back(g);
            }
        }
        // Bound discovery attempts as well as each individual connect. Never
        // try an audio service or guess raw RFCOMM channel numbers.
        const auto budget=std::clamp(options.openBudget,std::chrono::milliseconds(50),std::chrono::milliseconds(4000));
        auto deadline=Clock::now()+budget;
        for(auto const& service:services){
            if(Cancelled()||Clock::now()>=deadline)break;
            SOCKET s=::socket(AF_BTH,SOCK_STREAM,BTHPROTO_RFCOMM);if(s==INVALID_SOCKET)continue;
            {std::lock_guard lk(socketLock);socket=s;}
            u_long nonblocking=1;if(ioctlsocket(s,FIONBIO,&nonblocking)!=0){Close();continue;}
            SOCKADDR_BTH peer{};peer.addressFamily=AF_BTH;peer.btAddr=address;peer.serviceClassId=service;peer.port=0;
            int r=connect(s,reinterpret_cast<sockaddr*>(&peer),sizeof(peer));
            if(r==SOCKET_ERROR){
                int error=WSAGetLastError();state.errorCode=error;
                if(error!=WSAEWOULDBLOCK&&error!=WSAEINPROGRESS){Close();continue;}
                if(!Wait(s,true,std::min(deadline,Clock::now()+std::chrono::milliseconds(2000)))){Close();continue;}
                int status=0;int size=sizeof(status);
                if(getsockopt(s,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&status),&size)!=0||status!=0){state.errorCode=status;Close();continue;}
            }
            if(!Cancelled()){sessionGeneration=token;state.errorCode=0;return true;}Close();
        }
        return false;
    }
    std::vector<Frame> Exchange(const Bytes& request,std::chrono::milliseconds budget=std::chrono::milliseconds(1400),
                              const std::function<bool(std::span<const Frame>)>& received={}){
        std::vector<Frame> frames;if(socket==INVALID_SOCKET||Cancelled())return frames;
        const SOCKET s=socket;
        // Discard bytes from earlier transactions before a fresh readback. ACKs
        // and unrelated frames never count as a setting's confirmed value.
        char data[2048];for(int i=0;i<16;++i){int n=recv(s,data,sizeof(data),0);if(n<=0)break;}
        decoder.Reset();auto deadline=Clock::now()+budget;size_t sent=0;
        while(sent<request.size()&&!Cancelled()){
            int n=send(s,reinterpret_cast<const char*>(request.data()+sent),int(request.size()-sent),0);
            if(n>0){sent+=size_t(n);continue;}
            if(n==SOCKET_ERROR&&WSAGetLastError()==WSAEWOULDBLOCK&&Wait(s,true,deadline))continue;
            state.errorCode=WSAGetLastError();Close();return frames;
        }
        auto quietUntil=deadline;size_t total=0;
        while(!Cancelled()&&Clock::now()<std::min(deadline,quietUntil)){
            if(!Wait(s,false,std::min(deadline,quietUntil)))break;
            int n=recv(s,data,sizeof(data),0);
            if(n==0){Close();break;}
            if(n==SOCKET_ERROR){if(WSAGetLastError()==WSAEWOULDBLOCK)continue;Close();break;}
            total+=size_t(n);if(total>65536){Close();break;}
            auto next=decoder.Feed(std::span(reinterpret_cast<const uint8_t*>(data),size_t(n)));
            const bool complete=received&&!next.empty()&&received(next);
            frames.insert(frames.end(),std::make_move_iterator(next.begin()),std::make_move_iterator(next.end()));
            if(complete)break;
            if(!received&&!frames.empty())quietUntil=Clock::now()+std::chrono::milliseconds(250);
        }
        if(Cancelled())Close();return frames;
    }
    bool Apply(std::span<const Frame> frames){
        bool evidence=false;
        for(auto const& frame:frames){
            if(auto identity=ReadIdentity(frame)){
                if(state.resourceId!=identity->resourceId||state.model!=identity->model){state.values.clear();state.equalizer.reset();}
                state.resourceId=identity->resourceId;state.colorId=identity->colorId;state.model=identity->model;state.identityConfirmed=true;evidence=true;
            }
            if(auto battery=ReadBattery(frame)){state.battery=*battery;state.batteryReadTime=Clock::now();evidence=true;}
            for(auto const& c:ControlProfile(state.model))if(auto v=c.Parse(frame)){state.values[c.id]=*v;evidence=true;}
            if(auto eq=ReadEqualizer(frame)){state.equalizer=*eq;evidence=true;}
            if(state.model==Model::Buds7i)if(auto peer=ReadPairedPeer(frame)){
                auto old=std::find_if(state.peers.begin(),state.peers.end(),[&](auto const& p){return p.address==peer->address;});
                if(old!=state.peers.end())*old=*peer;else if(state.peers.size()<16)state.peers.push_back(*peer);
                evidence=true;
            }
        }
        return evidence;
    }
    Bytes Queries(bool full){
        Bytes bytes=BatteryQuery();if(!full)return bytes;
        std::set<Bytes> queries;
        for(auto const& c:ControlProfile(state.model))if(!c.query.empty())queries.insert(c.query);
        if(SupportsCustomEqualizer(state.model))queries.insert(Packet(0x2b,0x4a,{{2,{}}}));
        if(auto peers=PairedPeersQuery(state.model);!peers.empty())queries.insert(std::move(peers));
        for(auto const& q:queries)bytes.insert(bytes.end(),q.begin(),q.end());return bytes;
    }
    bool ReadConnection(const Client::Progress& progress,bool batteryOnly=false){
        state.identityConfirmed=false;
        identityReadPending=batteryOnly&&!settingsInitialized;
        state.battery={}; // This transaction may reuse a socket, never its old batteries.
        state.batteryReadTime={};
        auto bytes=batteryOnly?Bytes{}:IdentityQuery();auto battery=BatteryQuery();bytes.insert(bytes.end(),battery.begin(),battery.end());
        ConnectionReplyProgress completion;bool valid=false;
        Exchange(bytes,std::chrono::milliseconds(1400),[&](std::span<const Frame> frames){
            if(batteryOnly){
                bool changed=false;
                for(const auto& frame:frames)if(auto values=ReadBattery(frame)){
                    state.battery=*values;state.batteryReadTime=Clock::now();changed=true;
                }
                valid=valid||changed;
                if(changed&&progress)progress(state);
                // Unsolicited identity, settings and ACK frames cannot promote
                // the cached identity or complete this battery-only request.
                return changed;
            }
            const bool changed=Apply(frames);valid=changed||valid;
            // Publish only new, validated data. The UI can load artwork as soon
            // as identity arrives, without waiting for the battery response.
            if(changed&&progress)progress(state);
            return completion.Observe(frames);
        });
        return valid;
    }
    Snapshot Write(uint64_t address,const Bytes& packet,const Bytes& readback,std::string_view id,int value,
                   std::optional<std::array<int,10>> curve={}){
        state.failed=false;
        if(state.address!=address||!state.ready||socket==INVALID_SOCKET||sessionGeneration!=token||packet.empty()){
            state.failed=true;state.message=L"耳机连接已变化，请刷新后重试。";return state;
        }
        auto frames=Exchange(packet,std::chrono::milliseconds(900));Apply(frames);
        // Remove stale values before asking the device again. A successful send
        // alone is never reported as a successful setting change.
        state.values.erase(std::string(id));if(curve)state.equalizer.reset();
        if(!readback.empty()){frames=Exchange(readback);Apply(frames);}
        auto found=state.values.find(std::string(id));
        bool confirmed=found!=state.values.end()&&found->second==value;
        if(curve)confirmed=confirmed&&state.equalizer&&*curve==*state.equalizer;
        state.failed=!confirmed;state.ready=socket!=INVALID_SOCKET&&!Cancelled();
        state.message=confirmed?L"已更新耳机设置。":L"已发送设置，但尚未收到耳机的状态确认，请刷新检查。";
        return state;
    }
};
Client::Client():m_impl(std::make_unique<Impl>()){}
Client::~Client(){Cancel();m_impl->Close();}
void Client::Cancel(){
    m_impl->generation.fetch_add(1);std::lock_guard lk(m_impl->socketLock);
    if(m_impl->socket!=INVALID_SOCKET)shutdown(m_impl->socket,SD_BOTH);
}
void Client::ReleaseCancelled(){
    std::lock_guard lk(m_impl->operation);
    if(m_impl->sessionGeneration!=m_impl->generation.load()){
        m_impl->Close();m_impl->state.ready=false;
    }
}
std::vector<Device> Client::Devices(){
    std::vector<Device> out;
    BLUETOOTH_DEVICE_SEARCH_PARAMS params{sizeof(params)};
    params.fReturnAuthenticated=TRUE;params.fReturnRemembered=TRUE;params.fReturnConnected=TRUE;
    params.fIssueInquiry=FALSE;
    BLUETOOTH_DEVICE_INFO info{sizeof(info)};
    auto search=BluetoothFindFirstDevice(&params,&info);if(!search)return out;
    do{
        std::wstring name=info.szName;
        const bool headset=(info.ulClassofDevice&0x1f00)==0x400;
        if((info.fAuthenticated||info.fRemembered)&&(headset||ModelFromName(name)!=Model::Unknown||name.find(L"FreeBuds")!=std::wstring::npos||name.find(L"FreeClip")!=std::wstring::npos))
            out.push_back({info.Address.ullLong,std::move(name),info.fConnected!=FALSE});
        info={sizeof(info)};
    }while(BluetoothFindNextDevice(search,&info));
    BluetoothFindDeviceClose(search);
    std::stable_sort(out.begin(),out.end(),[](auto const& a,auto const& b){return a.connected>b.connected;});return out;
}
std::optional<Device> Client::PairedDevice(uint64_t address){
    if(!address||(address>>48)!=0)return {};
    auto info=Info(address);
    if(BluetoothGetDeviceInfo(nullptr,&info)!=ERROR_SUCCESS||(!info.fAuthenticated&&!info.fRemembered))return {};
    std::wstring name=info.szName;
    const bool headset=(info.ulClassofDevice&0x1f00)==0x400;
    if(!headset&&ModelFromName(name)==Model::Unknown&&name.find(L"FreeBuds")==std::wstring::npos&&name.find(L"FreeClip")==std::wstring::npos)return {};
    return Device{address,std::move(name),info.fConnected!=FALSE};
}
std::optional<Device> Client::ConnectedDevice(uint64_t address){
    auto device=PairedDevice(address);
    return device&&device->connected?device:std::nullopt;
}
Snapshot Client::Refresh(const Device& device,bool full,const Progress& progress){
    return RefreshImpl(device,full,progress,false);
}
Snapshot Client::NotificationState(const Device& device,ConnectionOptions options){
    return RefreshImpl(device,false,{},true,options);
}
Snapshot Client::RefreshImpl(const Device& device,bool full,const Progress& progress,bool notificationRead,ConnectionOptions options){
    std::lock_guard lk(m_impl->operation);auto& p=*m_impl;p.token=p.generation.load();
    bool fresh=p.state.address!=device.address||p.socket==INVALID_SOCKET||p.sessionGeneration!=p.token;
    if(fresh){p.state={};p.state.address=device.address;p.state.name=device.name;p.state.model=ModelFromName(device.name);
        if(!p.Open(device.address,options)){p.state.failed=true;p.state.message=L"无法连接耳机控制功能，请确认耳机已连接到本机后重试。";return p.state;}}
    bool valid=p.state.ready;
    if(notificationRead){
        // A notification needs only current identity and batteries. A fresh
        // socket must not trigger the page's subsequent full settings query.
        valid=p.ReadConnection(progress,options.batteryOnly)||valid;
    }else{
        if(fresh||p.identityReadPending)valid=p.ReadConnection(progress)||valid;
        const bool readProfile=full||fresh||!p.settingsInitialized;
        if(readProfile)p.state.peers.clear();
        if(progress&&!fresh&&!readProfile)valid=p.ReadConnection(progress)||valid;
        else {
            auto frames=p.Exchange(p.Queries(readProfile));
            const bool observed=p.Apply(frames);valid=observed||valid;
            if(readProfile&&observed&&p.socket!=INVALID_SOCKET&&!p.Cancelled())p.settingsInitialized=true;
        }
    }
    p.state.ready=valid&&p.socket!=INVALID_SOCKET&&!p.Cancelled();p.state.failed=!p.state.ready;
    if(!p.state.ready)p.state.message=L"耳机未返回可识别的控制数据，请确认设备状态后重试。";
    else if(p.state.model==Model::Unknown)p.state.message=L"已读取耳机信息；此型号暂未提供经过验证的控制设置。";
    else p.state.message=L"已连接；设置以耳机返回的状态为准。";
    return p.state;
}
Snapshot Client::ConnectionState(const Device& device,const Progress& progress,ConnectionOptions options){
    std::lock_guard lk(m_impl->operation);auto& p=*m_impl;p.token=p.generation.load();
    p.state={};p.state.address=device.address;p.state.name=device.name;p.state.model=ModelFromName(device.name);
    if(p.Open(device.address,options)){
        p.state.ready=p.ReadConnection(progress,options.batteryOnly);
    }
    p.Close();return p.state;
}
Snapshot Client::Set(uint64_t address,std::string_view id,int value){
    std::lock_guard lk(m_impl->operation);auto& p=*m_impl;p.token=p.generation.load();
    auto packet=WriteControl(p.state.model,id,value);
    if(packet)for(auto const& c:ControlProfile(p.state.model))if(c.id==id)return p.Write(address,*packet,c.query,id,value);
    auto result=p.state;result.failed=true;result.message=L"当前耳机不支持这项设置。";return result;
}
Snapshot Client::SetEqualizer(uint64_t address,int slot,std::string_view name,std::span<const int> gains){
    std::lock_guard lk(m_impl->operation);auto& p=*m_impl;p.token=p.generation.load();
    auto packet=CustomEqualizer(p.state.model,slot,name,gains);
    if(!packet){auto result=p.state;result.failed=true;result.message=L"均衡器设置无效，请检查名称和频段范围。";return result;}
    std::array<int,10> curve;std::copy(gains.begin(),gains.end(),curve.begin());
    return p.Write(address,*packet,Packet(0x2b,0x4a,{{2,{}}}),"equalizer",slot,curve);
}
Snapshot Client::RemovePeer(uint64_t address,const std::array<uint8_t,6>& peer){
    std::lock_guard lk(m_impl->operation);auto& p=*m_impl;p.token=p.generation.load();
    auto packet=ForgetPeer(p.state.model,peer);
    bool known=std::any_of(p.state.peers.begin(),p.state.peers.end(),[&](auto const& entry){return entry.address==peer;});
    if(!packet||!known||!p.state.ready||p.state.address!=address||p.sessionGeneration!=p.token){
        auto state=p.state;state.failed=true;state.message=L"设备列表已变化，请刷新后重试。";return state;
    }
    p.Exchange(*packet);p.state.peers.clear();auto frames=p.Exchange(PairedPeersQuery(p.state.model));p.Apply(frames);
    p.state.failed=false;p.state.ready=p.socket!=INVALID_SOCKET&&!p.Cancelled();
    p.state.message=L"已请求移除配对记录，请刷新列表确认；如连接断开，请重新连接耳机。";return p.state;
}
}
