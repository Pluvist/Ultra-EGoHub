#include "pch.h"
#include "MainWindow.xaml.h"
#include "HeadphoneImageLoader.h"
#include "HeadphoneImageIdentity.h"
#include "DeviceArtworkIdentityCache.h"
#include "PopupTimingTrace.h"
#include <chrono>
#include <atomic>

namespace winrt::EGoTouchSettings::implementation {
namespace {
std::string ImageKey(const HuaweiHeadphones::Snapshot& state){
    auto identity=HuaweiHeadphones::Images::ConfirmedIdentity(state.model,state.resourceId,state.colorId);
    return identity?*HuaweiHeadphones::Images::CacheKey(*identity):std::string{};
}
HuaweiHeadphones::Snapshot ArtworkOnly(const HuaweiHeadphones::Snapshot& state){
    HuaweiHeadphones::Snapshot out;
    out.address=state.address;out.name=state.name;out.model=state.model;
    out.resourceId=state.resourceId;out.colorId=state.colorId;
    return out;
}
HuaweiHeadphones::Snapshot DisplayWithArtwork(const HuaweiHeadphones::Snapshot& state,
                                             const HuaweiHeadphones::Snapshot& cached){
    auto out=state;
    if(!state.identityConfirmed&&state.address==cached.address&&!ImageKey(cached).empty()){
        out.model=cached.model;out.resourceId=cached.resourceId;out.colorId=cached.colorId;
    }
    return out;
}
fire_and_forget RememberVerifiedArtwork(uint64_t address,HuaweiHeadphones::Images::Identity identity){
    try{
        co_await resume_background();
        HuaweiHeadphones::Images::KnownDevices::Remember(address,identity);
    }catch(...){}
}
}
HuaweiHeadphones::Snapshot MainWindow::HeadphoneArtworkSnapshot(const HuaweiHeadphones::Snapshot& state) const{
    if(!state.identityConfirmed){
        if(auto known=m_headphoneIdentities.find(state.address);known!=m_headphoneIdentities.end())
            if(HuaweiHeadphones::Images::ConfirmedIdentity(HuaweiHeadphones::ModelFromName(state.name),known->second.resourceId,known->second.colorId))
                return DisplayWithArtwork(state,known->second);
    }
    return state;
}
void MainWindow::ApplyHeadphoneImage(const HuaweiHeadphones::Snapshot& state,const Media::ImageSource& source){
    if(m_closed)return;
    const auto current=HeadphoneArtworkSnapshot(m_headphoneSnapshot);
    if(state.address==current.address&&ImageKey(state)==ImageKey(current)){
        HeadphoneImage().Source(source);HeadphoneImage().Visibility(source?Visibility::Visible:Visibility::Collapsed);
        HeadphoneImageFallback().Visibility(source?Visibility::Collapsed:Visibility::Visible);
    }
    if(m_notificationWindow)get_self<implementation::NotificationWindow>(m_notificationWindow)->UpdateHeadphoneState(m_headphonePageVisible&&state.address==current.address&&ImageKey(state)==ImageKey(current)?current:state,source);
}
fire_and_forget MainWindow::LoadHeadphoneImageAsync(HuaweiHeadphones::Snapshot state){
    auto lifetime=get_strong();
    try{co_await PrepareHeadphoneImageAsync(std::move(state));}catch(...){}
}
Windows::Foundation::IAsyncAction MainWindow::PrepareHeadphoneImageAsync(HuaweiHeadphones::Snapshot state){
    if(m_closed)co_return;
    state=HeadphoneArtworkSnapshot(state);
    const auto key=ImageKey(state);
    if(key.empty()){ApplyHeadphoneImage(state,nullptr);co_return;}
    if(state.address&&state.identityConfirmed){
        // Keep verified artwork identity only. Batteries/settings must be read
        // again on reconnect rather than presented as a cached live state.
        m_headphoneIdentities.insert_or_assign(state.address,ArtworkOnly(state));
        while(m_headphoneIdentities.size()>8){auto first=m_headphoneIdentities.begin();if(first->first==state.address)++first;m_headphoneIdentities.erase(first);}
        if(auto identity=HuaweiHeadphones::Images::ConfirmedIdentity(state.model,state.resourceId,state.colorId))
            RememberVerifiedArtwork(state.address,*identity);
    }
    if(auto found=m_headphoneImages.find(key);found!=m_headphoneImages.end()){ApplyHeadphoneImage(state,found->second);co_return;}
    // Clear the previous device's image before starting a new decode.
    ApplyHeadphoneImage(state,nullptr);
    auto& requests=m_headphoneImageRequests[key];
    requests.insert_or_assign(state.address,ArtworkOnly(state));
    while(requests.size()>8){auto first=requests.begin();if(first->first==state.address)++first;requests.erase(first);}
    if(m_headphoneImagePending[key])co_return;
    m_headphoneImagePending[key]=true;
    auto lifetime=get_strong();auto ui=apartment_context();AccessoryImages::Asset asset;
    co_await resume_background();
    try{asset=AccessoryImages::ResolveHeadphone(state);}catch(...){}
    try{co_await ui;}catch(...){co_return;}
    if(m_closed)co_return;
    try{
        if(asset){
            Media::Imaging::SoftwareBitmapSource source;
            auto size=co_await AccessoryImages::DecodeCroppedAsync(std::move(asset.encoded),source);
            if(!m_closed&&size.Width>0&&size.Height>0){
                m_headphoneImages.insert_or_assign(key,source);
                while(m_headphoneImages.size()>2){auto first=m_headphoneImages.begin();if(first->first==key)++first;m_headphoneImages.erase(first);}
                // Two devices may share one product image. Notify every waiter;
                // each view still validates its current address and artwork key.
                if(auto waiting=m_headphoneImageRequests.find(key);waiting!=m_headphoneImageRequests.end())
                    for(const auto& [address,request]:waiting->second)ApplyHeadphoneImage(request,source);
            }
        }
    }catch(...){}
    m_headphoneImagePending[key]=false;
    m_headphoneImageRequests.erase(key);
}
void MainWindow::PresentHeadphoneConnection(const HuaweiHeadphones::Snapshot& state){
    if(m_closed||ReadUserSetting(L"DeviceNotifications",1)==0)return;
    EnsureNotificationWindow();
    auto window=get_self<implementation::NotificationWindow>(m_notificationWindow);
    window->ApplyTheme(RootLayout().RequestedTheme());
    Media::ImageSource source{nullptr};
    if(auto found=m_headphoneImages.find(ImageKey(state));found!=m_headphoneImages.end())source=found->second;
    window->ShowHeadphoneConnected(state,source);
}
void MainWindow::DispatchPendingHeadphonePopup(){
    if(m_closed||m_headphoneBusy||m_headphoneDialogPending||m_headphonePopupReading||!m_pendingHeadphonePopup)return;
    const auto address=m_pendingHeadphonePopup;
    const auto since=m_pendingHeadphoneSince;
    const auto generation=m_headphonePopupGeneration;
    m_pendingHeadphonePopup=0;m_pendingHeadphoneSince={};
    if(since!=std::chrono::steady_clock::time_point{}&&std::chrono::steady_clock::now()-since>std::chrono::seconds(8))return;
    DispatcherQueue().TryEnqueue([weak=get_weak(),address,since,generation]{
        if(auto self=weak.get();self&&!self->m_closed&&generation==self->m_headphonePopupGeneration)
            self->ShowHeadphoneConnectionAsync(address,since);
    });
}
fire_and_forget MainWindow::ShowHeadphoneConnectionAsync(uint64_t address,std::chrono::steady_clock::time_point requestedAt){
    PopupTimingTrace::Record("headphone.prepare");
    using Clock=std::chrono::steady_clock;
    if(!address||(address>>48)!=0||m_closed||ReadUserSetting(L"DeviceNotifications",1)==0)co_return;
    if(requestedAt==Clock::time_point{})requestedAt=Clock::now();
    if(m_headphonePopupReading){
        m_pendingHeadphonePopup=address;m_pendingHeadphoneSince=requestedAt;++m_headphonePopupGeneration;co_return;
    }
    auto lifetime=get_strong();auto ui=apartment_context();const auto generation=++m_headphonePopupGeneration;
    const auto current=[this,generation]{return !m_closed&&generation==m_headphonePopupGeneration&&ReadUserSetting(L"DeviceNotifications",1)!=0;};
    const auto dispatchDeferred=[this]{
        if(m_closed||m_headphonePopupReading)return;
        if(m_pendingHeadphonePopup)DispatchPendingHeadphonePopup();
        else if(m_headphoneRefreshDeferred&&m_headphonePageVisible){m_headphoneRefreshDeferred=false;RefreshHeadphonesAsync(false);}
    };
    HuaweiHeadphones::Device device;bool paired=false;
    std::optional<HuaweiHeadphones::Images::Identity> stored;
    co_await resume_background();
    // The tray has a real ACL event. fConnected is only a lagging cache hint;
    // it must not veto that event. Pairing/type validation remains mandatory.
    for(int attempt=0;attempt<3&&!paired;++attempt){
        try{if(auto found=HuaweiHeadphones::Client::PairedDevice(address)){
            device=*found;paired=true;stored=HuaweiHeadphones::Images::KnownDevices::Load(address);
        }}catch(...){}
        if(!paired&&attempt<2)co_await resume_after(std::chrono::milliseconds(100));
    }
    try{co_await ui;}catch(...){co_return;}
    PopupTimingTrace::Record("headphone.paired",paired?1:0);
    if(!current()||!paired)co_return;
    HuaweiHeadphones::Snapshot cached;
    const auto namedModel=HuaweiHeadphones::ModelFromName(device.name);
    const auto useIdentity=[&](std::string_view resource,std::string_view color){
        if(auto identity=HuaweiHeadphones::Images::ConfirmedIdentity(namedModel,resource,color)){
            cached.resourceId=identity->modelId;cached.colorId=identity->colorId;
            cached.model=HuaweiHeadphones::ModelFromResource(identity->modelId);return true;
        }
        return false;
    };
    bool known=false;
    if(auto found=m_headphoneIdentities.find(address);found!=m_headphoneIdentities.end())known=useIdentity(found->second.resourceId,found->second.colorId);
    if(!known&&m_headphoneSnapshot.address==address)known=useIdentity(m_headphoneSnapshot.resourceId,m_headphoneSnapshot.colorId);
    if(!known&&stored)useIdentity(stored->modelId,stored->colorId);
    cached.address=address;cached.name=device.name;
    Windows::Foundation::IAsyncAction cachedPreparation{nullptr};
    if(!ImageKey(cached).empty()){
        m_headphoneIdentities.insert_or_assign(address,ArtworkOnly(cached));
        while(m_headphoneIdentities.size()>8){auto first=m_headphoneIdentities.begin();if(first->first==address)++first;m_headphoneIdentities.erase(first);}
        try{cachedPreparation=PrepareHeadphoneImageAsync(cached);}catch(...){}
    }
    const auto busyDeadline=Clock::now()+std::chrono::milliseconds(2500);
    PopupTimingTrace::Record("headphone.busy",m_headphoneBusy?1:0);
    while(m_headphoneBusy||m_headphoneDialogPending){
        if(!current())co_return;
        if(Clock::now()>=busyDeadline){
            // The page completion hook retries this event instead of consuming
            // it. Its original time lets us reuse only a truly new battery read.
            m_pendingHeadphonePopup=address;m_pendingHeadphoneSince=requestedAt;co_return;
        }
        co_await resume_after(std::chrono::milliseconds(30));
        try{co_await ui;}catch(...){co_return;}
    }
    if(!current())co_return;
    const bool reuse=m_headphonePageVisible&&m_headphoneAddress==address;
    const auto epoch=m_headphoneUiEpoch;
    HuaweiHeadphones::Snapshot state;
    bool readAlreadyComplete=reuse&&m_headphoneSnapshot.ready&&m_headphoneSnapshot.batteryReadTime>=requestedAt;
    PopupTimingTrace::Record("headphone.read_start",readAlreadyComplete?1:0);
    if(readAlreadyComplete)state=m_headphoneSnapshot;
    else{
        auto client=reuse?m_headphoneClient:std::make_shared<HuaweiHeadphones::Client>();
        m_headphonePopupReading=true;SetHeadphoneBusy(m_headphoneBusy);
        DispatcherQueue().TryEnqueue([weak=get_weak(),generation]{
            if(auto self=weak.get();self&&!self->m_closed&&generation==self->m_headphonePopupGeneration){
                self->EnsureNotificationWindow();
                PopupTimingTrace::Record("headphone.host_ready");
            }
        });
        co_await resume_background();
        const auto began=Clock::now();
        auto options=HuaweiHeadphones::NotificationConnectionOptions;
        options.batteryOnly=!ImageKey(cached).empty();
        try{
            state=reuse?client->NotificationState(device,options):client->ConnectionState(device,{},options);
            const auto hasData=[](const auto& snapshot){return snapshot.identityConfirmed||std::any_of(snapshot.battery.begin(),snapshot.battery.end(),[](const auto& battery){return battery.level.has_value();});};
            if(!hasData(state)&&Clock::now()-began<std::chrono::milliseconds(600)){
                co_await resume_after(std::chrono::milliseconds(150));
                state=reuse?client->NotificationState(device,options):client->ConnectionState(device,{},options);
            }
            paired=HuaweiHeadphones::Client::PairedDevice(address).has_value();
        }catch(...){}
        try{co_await ui;}catch(...){co_return;}
        m_headphonePopupReading=false;
        if(m_closed)co_return;
        SetHeadphoneBusy(m_headphoneBusy);
    }
    if(!current()||!paired){dispatchDeferred();co_return;}
    PopupTimingTrace::Record("headphone.read_done",state.errorCode);
    // A connection event is still useful when the optional control service is
    // unavailable. Unread batteries stay unknown, never copied from the cache.
    state.address=address;state.name=device.name;
    if(!state.identityConfirmed&&state.resourceId.empty())state.model=namedModel;
    if(reuse&&epoch==m_headphoneUiEpoch&&!readAlreadyComplete&&state.ready){m_headphoneSnapshot=state;RenderHeadphones();}
    const auto display=DisplayWithArtwork(state,cached);
    const auto key=ImageKey(display);
    Windows::Foundation::IAsyncAction finalPreparation{nullptr};
    try{finalPreparation=PrepareHeadphoneImageAsync(display);}catch(...){}
    // Cached preparation has run alongside the radio work. Do not add a second
    // five-second serial wait: a failed/slow optional image becomes a static icon.
    const auto imageDeadline=Clock::now()+std::chrono::milliseconds(500);
    while(!key.empty()&&current()){
        const auto pending=m_headphoneImagePending.find(key);
        if(pending==m_headphoneImagePending.end()||!pending->second||Clock::now()>=imageDeadline)break;
        co_await resume_after(std::chrono::milliseconds(10));
        try{co_await ui;}catch(...){co_return;}
    }
    if(current()){
        PopupTimingTrace::Record("headphone.present");
        PresentHeadphoneConnection(display);
    }
    dispatchDeferred();
}
}
