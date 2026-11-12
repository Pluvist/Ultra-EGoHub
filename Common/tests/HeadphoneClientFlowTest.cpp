// Exercise the real client/protocol flow with no radio or network calls.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2bth.h>
#include <windows.h>
#include <bluetoothapis.h>
#include "../../Tools/EGoTouchSettings/HeadphoneClient.h"
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace Test {
using namespace HuaweiHeadphones;
struct Fixture {
    bool authenticated=true, remembered=false, connected=false, headset=true;
    bool connectFails=false, validIdentity=true, validBattery=true;
    bool waitForTimeout=false, unsolicitedIdentity=false;
    std::wstring name=L"FreeBuds Pro 5";
    int error=WSAEWOULDBLOCK, infoCalls=0, serviceEnumerations=0;
    int profileQueries=0, writes=0, readbacks=0;
    int emptyReadWaits=0;
    std::vector<ULONG> attemptedServices;
    std::vector<size_t> requestFrameCounts;
    Bytes pending;
} fixture;
int Startup(WORD,WSADATA*){return 0;}
int Cleanup(){return 0;}
int LastError(){return fixture.error;}
DWORD DeviceInfo(HANDLE,PBLUETOOTH_DEVICE_INFO info){
    ++fixture.infoCalls;
    info->fAuthenticated=fixture.authenticated;info->fRemembered=fixture.remembered;
    info->fConnected=fixture.connected;info->ulClassofDevice=fixture.headset?0x400:0;
    wcscpy_s(info->szName,fixture.name.c_str());return ERROR_SUCCESS;
}
DWORD Services(HANDLE,const BLUETOOTH_DEVICE_INFO*,DWORD* count,GUID* services){
    ++fixture.serviceEnumerations;
    if(!services){*count=2;return ERROR_MORE_DATA;}
    services[0]={0x1000,0,0x1000,{0x80,0,0,0x80,0x5f,0x9b,0x34,0xfb}};
    services[1]={0xfd9a,0,0x1000,{0x80,0,0,0x80,0x5f,0x9b,0x34,0xfb}};
    *count=2;return ERROR_SUCCESS;
}
SOCKET Socket(int,int,int){return 123;}
int Close(SOCKET){fixture.pending.clear();return 0;}
int Shutdown(SOCKET,int){return 0;}
int Ioctl(SOCKET,long,u_long*){return 0;}
int Connect(SOCKET,const sockaddr* peer,int){
    const auto& address=*reinterpret_cast<const SOCKADDR_BTH*>(peer);
    if(address.port!=0)throw std::runtime_error("Client guessed a raw channel");
    fixture.attemptedServices.push_back(address.serviceClassId.Data1);
    fixture.error=WSAECONNREFUSED;return fixture.connectFails?SOCKET_ERROR:0;
}
int Select(int,fd_set* read,fd_set* write,fd_set* error,const timeval*){
    if(error)FD_ZERO(error);
    if(read&&!write&&fixture.pending.empty()&&fixture.waitForTimeout){
        ++fixture.emptyReadWaits;Sleep(10);return 0;
    }
    return write||(!fixture.pending.empty()&&read)?1:SOCKET_ERROR;
}
int Sockopt(SOCKET,int,int,char* value,int*){*reinterpret_cast<int*>(value)=0;return 0;}
int Receive(SOCKET,char* bytes,int length,int){
    if(fixture.pending.empty()){fixture.error=WSAEWOULDBLOCK;return SOCKET_ERROR;}
    const size_t count=std::min(static_cast<size_t>(length),fixture.pending.size());
    std::memcpy(bytes,fixture.pending.data(),count);
    fixture.pending.erase(fixture.pending.begin(),fixture.pending.begin()+static_cast<ptrdiff_t>(count));
    return static_cast<int>(count);
}
int Send(SOCKET,const char* input,int length,int){
    Decoder decoder;const auto frames=decoder.Feed(std::span(reinterpret_cast<const uint8_t*>(input),static_cast<size_t>(length)));
    fixture.requestFrameCounts.push_back(frames.size());
    auto append=[](const Bytes& bytes){fixture.pending.insert(fixture.pending.end(),bytes.begin(),bytes.end());};
    for(const auto& frame:frames){
        if(frame.service==1&&frame.command==7&&fixture.validIdentity)
            append(Packet(1,7,{{2,{1,0x6d}},{0xa,Bytes{'T','V','E','-','0','0','0','1','6','D'}},{0xf,Bytes{'T','V','E'}},{0x19,{0x17}}}));
        else if(frame.service==1&&frame.command==8){
            if(fixture.unsolicitedIdentity){
                append(Packet(1,7,{{2,{1,0x6d}},{0xa,Bytes{'T','V','E','-','0','0','0','1','6','D'}},{0xf,Bytes{'T','V','E'}},{0x19,{0x17}}}));
                append(Packet(0x2b,0x2a,{{1,{0,0}}}));
            }
            append(Packet(1,8,{{2,fixture.validBattery?Bytes{81,82,83}:Bytes{101,102,255}}}));
        }
        else {
            ++fixture.profileQueries;
            if(frame.service==0x2b&&frame.command==4){++fixture.writes;append(Packet(0x2b,4,{{1,{0}}}));}
            if(frame.service==0x2b&&frame.command==0x2a){++fixture.readbacks;append(Packet(0x2b,0x2a,{{1,{0,0}}}));}
        }
    }
    return length;
}
}

// Headers are already included, so only calls in the implementation are replaced.
#define WSAStartup Test::Startup
#define WSACleanup Test::Cleanup
#define WSAGetLastError Test::LastError
#define BluetoothGetDeviceInfo Test::DeviceInfo
#define BluetoothEnumerateInstalledServices Test::Services
#define socket Socket
#define closesocket Test::Close
#define shutdown Test::Shutdown
#define ioctlsocket Test::Ioctl
#define connect Test::Connect
#define select Test::Select
#define getsockopt Test::Sockopt
#define recv Test::Receive
#define send Test::Send
// ::socket is the only qualified call. Keep that replacement a global function.
static SOCKET Socket(int a,int b,int c){return Test::Socket(a,b,c);}
#include "../../Tools/EGoTouchSettings/HeadphoneClient.cpp"
#undef socket
#undef connect
#undef select
#undef send
#undef recv

static void Check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}

int main(){try{
    using namespace HuaweiHeadphones;
    constexpr uint64_t address=0x123456789abc;
    Check(!Client::PairedDevice(0)&&!Client::PairedDevice(uint64_t{1}<<48)&&Test::fixture.infoCalls==0,"Invalid addresses bypass the OS");
    const auto paired=Client::PairedDevice(address);
    Check(paired&&!paired->connected&&!Client::ConnectedDevice(address),"Paired device survives lagging connected flag");
    Test::fixture.connected=true;Check(Client::ConnectedDevice(address).has_value(),"ConnectedDevice retains connection filtering");
    Test::fixture.authenticated=false;Check(!Client::PairedDevice(address),"Connected but unpaired device rejected");
    Test::fixture.remembered=true;Check(Client::PairedDevice(address).has_value(),"Remembered pairing accepted");
    Test::fixture.headset=false;Test::fixture.name=L"Unrelated device";Check(!Client::PairedDevice(address),"Non-headset unknown device rejected");
    Test::fixture.name=L"FreeBuds Pro 5";Check(Client::PairedDevice(address).has_value(),"Known headset name remains eligible");
    Test::fixture={};
    const Device device{address,L"FreeBuds Pro 5",false};
    {
        Client client;const auto before=std::chrono::steady_clock::now();
        const auto notification=client.NotificationState(device,NotificationConnectionOptions);
        Check(notification.ready&&notification.identityConfirmed&&notification.batteryReadTime>=before&&
            notification.batteryReadTime<=std::chrono::steady_clock::now(),"Only valid battery data acquires a fresh steady timestamp");
        Check(Test::fixture.profileQueries==0&&Test::fixture.serviceEnumerations==0&&Test::fixture.requestFrameCounts==std::vector<size_t>{2},
            "Fresh notification reads identity and battery once, skips settings and other services");
        const auto normal=client.Refresh(device,false,[](const Snapshot&){});
        Check(normal.ready&&Test::fixture.profileQueries>0,"Page fills the full settings profile after notification-created session");
        const int priorProfile=Test::fixture.profileQueries;
        client.Refresh(device,false);
        Check(Test::fixture.profileQueries==priorProfile&&Test::fixture.requestFrameCounts.back()==1,"Initialized page returns to battery-only refresh");
        const auto written=client.Set(address,"noise",0);
        Check(!written.failed&&Test::fixture.writes==1&&Test::fixture.readbacks>=2,"Control write still requires a matching readback");
        Test::fixture.validBattery=false;
        const auto identityOnly=client.NotificationState(device,NotificationConnectionOptions);
        Check(identityOnly.identityConfirmed&&identityOnly.batteryReadTime==std::chrono::steady_clock::time_point{}&&
            !identityOnly.battery[0].level,"Identity/ACK/unknown battery cannot retain or advance stale battery time");
    }
    ConnectionOptions batteryOptions=NotificationConnectionOptions;batteryOptions.batteryOnly=true;
    Test::fixture={};Test::fixture.waitForTimeout=true;Test::fixture.unsolicitedIdentity=true;
    {
        Client client;const auto start=std::chrono::steady_clock::now();
        const auto batteryOnly=client.NotificationState(device,batteryOptions);
        Check(batteryOnly.ready&&batteryOnly.battery[0].level==81&&batteryOnly.batteryReadTime>=start&&
            !batteryOnly.identityConfirmed&&batteryOnly.resourceId.empty()&&batteryOnly.values.empty(),
            "Cached battery-only read never promotes unsolicited identity/settings or invents cache evidence");
        Check(Test::fixture.requestFrameCounts==std::vector<size_t>{1}&&Test::fixture.profileQueries==0&&
            Test::fixture.serviceEnumerations==0&&Test::fixture.emptyReadWaits==0,
            "Cached notification sends one battery frame and completes immediately on valid battery data");
        client.Refresh(device,false,[](const Snapshot&){});
        Check(Test::fixture.requestFrameCounts.size()==3&&Test::fixture.requestFrameCounts[1]==2&&
            Test::fixture.profileQueries>0,"Page still reads identity and initializes settings after battery-only notification");
    }
    Test::fixture={};Test::fixture.waitForTimeout=true;Test::fixture.unsolicitedIdentity=true;Test::fixture.validBattery=false;
    {
        Client client;const auto start=std::chrono::steady_clock::now();
        const auto missing=client.ConnectionState(device,{},batteryOptions);
        const auto elapsed=std::chrono::steady_clock::now()-start;
        Check(!missing.ready&&!missing.identityConfirmed&&missing.resourceId.empty()&&missing.values.empty()&&
            !missing.battery[0].level&&missing.batteryReadTime==std::chrono::steady_clock::time_point{},
            "Unsolicited identity and invalid battery cannot satisfy the cached battery-only read");
        Check(elapsed>=std::chrono::milliseconds(1300)&&elapsed<std::chrono::milliseconds(3500)&&
            Test::fixture.emptyReadWaits>0&&Test::fixture.requestFrameCounts==std::vector<size_t>{1},
            "Missing valid battery uses the original 1.4-second deadline without fabricating values");
    }
    Test::fixture={};Test::fixture.connectFails=true;
    {Client client;client.ConnectionState(device,{},NotificationConnectionOptions);}
    Check(Test::fixture.attemptedServices==std::vector<ULONG>{0x1101}&&Test::fixture.serviceEnumerations==0,"Notification failure never scans discovery or FD9A services");
    Test::fixture={};Test::fixture.connectFails=true;
    {Client client;client.Refresh(device,false);}
    Check(Test::fixture.attemptedServices==std::vector<ULONG>{0x1101,0x1000,0xfd9a}&&Test::fixture.serviceEnumerations==2,
        "Default page transport retains installed-service discovery");
    std::cout<<"Headphone paired lookup, notification transport, profile initialization, battery freshness and write/readback flows passed without hardware\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
