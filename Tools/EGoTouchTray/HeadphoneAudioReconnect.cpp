#include "HeadphoneAudioReconnect.h"
#include "HeadphoneConnectionPolicy.h"

#include <bluetoothapis.h>
#include <cfgmgr32.h>
#include <devicetopology.h>
#include <devpkey.h>
#include <propkeydef.h>
#include <propkey.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmdeviceapi.h>
#include <ks.h>
#include <ksmedia.h>
#include <strmif.h>
#include <ksproxy.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cwchar>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace HeadphoneAudioReconnect {
namespace {
using Microsoft::WRL::ComPtr;
constexpr UINT kMaxEndpoints=128;
constexpr UINT kMaxConnectors=16;
constexpr size_t kMaxIdentifier=4096;
constexpr ULONG kMaxIdentifierList=65536;
constexpr size_t kMaxBthNodes=512;
constexpr HRESULT kUnsupported=HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
constexpr HRESULT kNotReady=HRESULT_FROM_WIN32(ERROR_NOT_READY);

bool DefiniteUnsupported(HRESULT error){
    return error==E_NOINTERFACE||error==E_NOTIMPL||error==kUnsupported;
}
Result Unavailable(HRESULT error){
    return {DefiniteUnsupported(error)?Status::Unsupported:Status::Failed,
            FAILED(error)?error:kNotReady};
}
struct Failures {
    HRESULT transient=kNotReady,unsupported=kUnsupported;
    bool sawTransient=false,sawUnsupported=false;
    void Observe(HRESULT error){
        if(error==S_FALSE)return; // A different device is not a failed target.
        if(DefiniteUnsupported(error)){unsupported=error;sawUnsupported=true;}
        else {transient=FAILED(error)?error:kNotReady;sawTransient=true;}
    }
    HRESULT Error() const{return sawTransient?transient:sawUnsupported?unsupported:kNotReady;}
};

struct ComApartment {
    HRESULT result=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    ~ComApartment(){if(SUCCEEDED(result))CoUninitialize();}
};
struct TaskString {
    LPWSTR value=nullptr;
    ~TaskString(){CoTaskMemFree(value);}
};
struct PropertyValue {
    PROPVARIANT value{};
    ~PropertyValue(){PropVariantClear(&value);}
};

Result ValidatePair(uint64_t address){
    if(!address||(address>>48)!=0)return {Status::NotPaired,E_INVALIDARG};
    BLUETOOTH_DEVICE_INFO info{sizeof(info)};info.Address.ullLong=address;
    const DWORD error=BluetoothGetDeviceInfo(nullptr,&info);
    if(error!=ERROR_SUCCESS){
        return {Status::Failed,HRESULT_FROM_WIN32(error)};
    }
    if(!info.fAuthenticated&&!info.fRemembered)return {Status::NotPaired,HRESULT_FROM_WIN32(ERROR_NOT_FOUND)};
    if(info.szName[0]==L'\0')return {Status::Failed,kNotReady};
    if(!HeadphoneConnections::IsHuaweiHeadphoneName(info.szName))return {Status::Unsupported,kUnsupported};
    // fConnected is an ACL flag, not evidence that the audio render path is up.
    return {Status::Requested,S_OK};
}

wchar_t UpperAscii(wchar_t c){return c>=L'a'&&c<=L'z'?static_cast<wchar_t>(c-L'a'+L'A'):c;}
std::optional<uint64_t> ParseBluetoothAddress(std::wstring_view value){
    if(value.size()!=12&&value.size()!=17)return {};
    const wchar_t separator=value.size()==17?value[2]:0;
    if(separator&&separator!=L':'&&separator!=L'-')return {};
    uint64_t address=0;
    for(size_t index=0;index<value.size();++index){
        wchar_t c=value[index];
        if(value.size()==17&&index%3==2){if(c!=separator)return {};continue;}
        c=UpperAscii(c);unsigned digit=0;
        if(c>=L'0'&&c<=L'9')digit=static_cast<unsigned>(c-L'0');
        else if(c>=L'A'&&c<=L'F')digit=static_cast<unsigned>(c-L'A'+10);
        else return {};
        address=(address<<4)|digit;
    }
    return address?std::optional<uint64_t>{address}:std::nullopt;
}

std::optional<uint64_t> NodeBluetoothAddress(DEVINST node){
    // This official device-interface property is also exposed on the native
    // BTHENUM devnodes by the Windows Bluetooth stack (verified read-only).
    const DEVPROPKEY key{PKEY_DeviceInterface_Bluetooth_DeviceAddress.fmtid,
                         PKEY_DeviceInterface_Bluetooth_DeviceAddress.pid};
    DEVPROPTYPE type=0;std::array<wchar_t,64> value{};ULONG bytes=sizeof(value);
    const CONFIGRET status=CM_Get_DevNode_PropertyW(node,&key,&type,
        reinterpret_cast<PBYTE>(value.data()),&bytes,0);
    if(status!=CR_SUCCESS||type!=DEVPROP_TYPE_STRING||bytes<sizeof(wchar_t)||
       bytes>sizeof(value)||bytes%sizeof(wchar_t)!=0)return {};
    const size_t count=bytes/sizeof(wchar_t);
    const size_t length=wcsnlen_s(value.data(),count);
    if(length+1!=count)return {};
    return ParseBluetoothAddress(std::wstring_view{value.data(),length});
}

bool DeviceNode(IMMDevice* device,DEVINST& node,HRESULT& error){
    ComPtr<IPropertyStore> properties;
    error=device->OpenPropertyStore(STGM_READ,&properties);if(FAILED(error))return false;
    PropertyValue instance;
    error=properties->GetValue(PKEY_Device_InstanceId,&instance.value);if(FAILED(error))return false;
    if(instance.value.vt==VT_EMPTY||(instance.value.vt==VT_LPWSTR&&!instance.value.pwszVal)){error=kNotReady;return false;}
    if(instance.value.vt!=VT_LPWSTR){error=kUnsupported;return false;}
    const size_t length=wcsnlen_s(instance.value.pwszVal,kMaxIdentifier+1);
    if(!length){error=kNotReady;return false;}
    if(length>kMaxIdentifier){error=kUnsupported;return false;}
    // A registered but unplugged audio adapter can have a non-present devnode.
    // PHANTOM locates that cached node; it does not enable or create a device.
    const CONFIGRET status=CM_Locate_DevNodeW(&node,instance.value.pwszVal,CM_LOCATE_DEVNODE_PHANTOM);
    if(status!=CR_SUCCESS){error=HRESULT_FROM_WIN32(CM_MapCrToWin32Err(status,ERROR_NOT_FOUND));return false;}
    error=S_OK;return true;
}

bool NodeContainer(DEVINST node,GUID& container,HRESULT& error){
    DEVPROPTYPE type=0;ULONG bytes=sizeof(container);container={};
    // PKEY and DEVPKEY_Device_ContainerId have the same SDK format ID and PID.
    const DEVPROPKEY key{PKEY_Device_ContainerId.fmtid,PKEY_Device_ContainerId.pid};
    const CONFIGRET status=CM_Get_DevNode_PropertyW(node,&key,&type,
        reinterpret_cast<PBYTE>(&container),&bytes,0);
    if(status!=CR_SUCCESS){error=HRESULT_FROM_WIN32(CM_MapCrToWin32Err(status,ERROR_NOT_FOUND));return false;}
    if(type==DEVPROP_TYPE_EMPTY||(type==DEVPROP_TYPE_GUID&&bytes==sizeof(container)&&IsEqualGUID(container,GUID_NULL))){
        error=kNotReady;return false;
    }
    if(type!=DEVPROP_TYPE_GUID||bytes!=sizeof(container)){
        error=kUnsupported;return false;
    }
    error=S_OK;return true;
}

struct Target {uint64_t address=0;GUID container{};};
bool ResolveTarget(uint64_t address,Target& target,HRESULT& error){
    ULONG characters=0;
    CONFIGRET status=CM_Get_Device_ID_List_SizeW(&characters,L"BTHENUM",CM_GETIDLIST_FILTER_ENUMERATOR);
    if(status!=CR_SUCCESS){error=HRESULT_FROM_WIN32(CM_MapCrToWin32Err(status,ERROR_NOT_FOUND));return false;}
    if(characters<2){error=kNotReady;return false;}
    if(characters>kMaxIdentifierList){error=kUnsupported;return false;}
    std::vector<wchar_t> identifiers(characters);
    status=CM_Get_Device_ID_ListW(L"BTHENUM",identifiers.data(),characters,CM_GETIDLIST_FILTER_ENUMERATOR);
    if(status!=CR_SUCCESS){error=HRESULT_FROM_WIN32(CM_MapCrToWin32Err(status,ERROR_NOT_FOUND));return false;}
    struct Remote {uint64_t address;GUID container;};
    std::vector<Remote> remotes;
    size_t offset=0,nodeCount=0;bool terminated=false,found=false;
    while(offset<identifiers.size()){
        const size_t remaining=identifiers.size()-offset;
        const size_t length=wcsnlen_s(identifiers.data()+offset,remaining);
        if(length==0){terminated=true;break;}
        if(length>=remaining||length>kMaxIdentifier||++nodeCount>kMaxBthNodes){error=kUnsupported;return false;}
        DEVINST node=0;
        status=CM_Locate_DevNodeW(&node,identifiers.data()+offset,CM_LOCATE_DEVNODE_PHANTOM);
        if(status==CR_SUCCESS){
            if(const auto remoteAddress=NodeBluetoothAddress(node)){
                GUID container{};
                if(NodeContainer(node,container,error)){
                remotes.push_back({*remoteAddress,container});
                if(*remoteAddress==address){
                    if(found&&!IsEqualGUID(target.container,container)){error=kUnsupported;return false;}
                    target={address,container};found=true;
                }
                }
            }
        }
        offset+=length+1;
    }
    if(!terminated){error=kUnsupported;return false;}
    if(!found){error=kNotReady;return false;}
    // A shared/radio-wide or otherwise ambiguous container is not evidence of
    // one remote device. Refuse it even if one of the addresses is our target.
    for(const auto& remote:remotes)if(remote.address!=address&&IsEqualGUID(remote.container,target.container)){
        error=kUnsupported;return false;
    }
    error=S_OK;return true;
}

bool MatchesTarget(IMMDevice* device,const Target& target,HRESULT& error){
    DEVINST node=0;GUID container{};
    if(!DeviceNode(device,node,error)||!NodeContainer(node,container,error))return false;
    const bool same=IsEqualGUID(container,target.container)!=FALSE;
    error=same?S_OK:kUnsupported;return same;
}

bool EndpointMatchesTarget(IMMDevice* endpoint,const Target& target,HRESULT& error){
    ComPtr<IPropertyStore> properties;error=endpoint->OpenPropertyStore(STGM_READ,&properties);
    if(FAILED(error))return false;
    PropertyValue value;error=properties->GetValue(PKEY_Device_ContainerId,&value.value);
    if(FAILED(error))return false;
    // The endpoint's Core Audio container is the underlying physical device.
    // Its SWD devnode can have a different software-container identity.
    if(value.value.vt==VT_EMPTY||(value.value.vt==VT_CLSID&&
       (!value.value.puuid||IsEqualGUID(*value.value.puuid,GUID_NULL)))){
        error=kNotReady;return false;
    }
    if(value.value.vt!=VT_CLSID){
        error=kUnsupported;return false;
    }
    const bool same=IsEqualGUID(*value.value.puuid,target.container)!=FALSE;
    error=same?S_OK:S_FALSE;return same;
}

struct Candidate {
    ComPtr<IMMDevice> endpoint;
    ComPtr<IMMDevice> filter;
};

// Read-only endpoint -> connector -> adapter topology traversal. Only the
// adapter's own IMMDevice may subsequently activate the filter-level IKsControl.
bool CollectCandidates(IMMDeviceEnumerator* enumerator,IMMDevice* endpoint,
                       const Target& target,std::vector<Candidate>& candidates,HRESULT& error,
                       bool* matchedEndpoint=nullptr){
    if(matchedEndpoint)*matchedEndpoint=false;
    if(!EndpointMatchesTarget(endpoint,target,error))return false;
    if(matchedEndpoint)*matchedEndpoint=true;
    ComPtr<IDeviceTopology> topology;
    error=endpoint->Activate(__uuidof(IDeviceTopology),CLSCTX_INPROC_SERVER,nullptr,
                             reinterpret_cast<void**>(topology.GetAddressOf()));
    if(FAILED(error))return false;
    UINT count=0;error=topology->GetConnectorCount(&count);
    if(FAILED(error))return false;
    if(!count){error=kNotReady;return false;}
    if(count>kMaxConnectors){error=kUnsupported;return false;}
    bool matched=false;
    Failures failures;
    for(UINT index=0;index<count;++index){
        ComPtr<IConnector> connector,connected;
        error=topology->GetConnector(index,&connector);if(FAILED(error)){failures.Observe(error);continue;}
        error=connector->GetConnectedTo(&connected);if(FAILED(error)){failures.Observe(error);continue;}
        ComPtr<IPart> part;error=connected.As(&part);if(FAILED(error)){failures.Observe(error);continue;}
        ComPtr<IDeviceTopology> adapterTopology;
        error=part->GetTopologyObject(&adapterTopology);if(FAILED(error)){failures.Observe(error);continue;}
        TaskString id;error=adapterTopology->GetDeviceId(&id.value);
        if(FAILED(error)||!id.value||id.value[0]==L'\0'){failures.Observe(FAILED(error)?error:kNotReady);continue;}
        if(wcsnlen_s(id.value,kMaxIdentifier+1)>kMaxIdentifier){failures.Observe(kUnsupported);continue;}
        ComPtr<IMMDevice> filter;error=enumerator->GetDevice(id.value,&filter);
        if(FAILED(error)){failures.Observe(error);continue;}
        if(!MatchesTarget(filter.Get(),target,error)){failures.Observe(error);continue;}
        matched=true;
        // Deduplicate the same filter COM object only within this endpoint.
        const auto same=std::find_if(candidates.begin(),candidates.end(),[&](const Candidate& value){
            return value.endpoint.Get()==endpoint&&value.filter.Get()==filter.Get();
        });
        if(same==candidates.end())candidates.push_back({endpoint,filter});
    }
    error=matched?S_OK:failures.Error();return matched;
}

Result RequestImpl(uint64_t address){
    const Result pair=ValidatePair(address);if(pair.status!=Status::Requested)return pair;
    Target target;HRESULT error=S_OK;
    if(!ResolveTarget(address,target,error))return Unavailable(error);
    ComApartment apartment;if(FAILED(apartment.result))return {Status::Failed,apartment.result};
    ComPtr<IMMDeviceEnumerator> enumerator;
    error=CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(enumerator.GetAddressOf()));
    if(FAILED(error))return {Status::Failed,error};
    ComPtr<IMMDeviceCollection> endpoints;
    // ACTIVE is inspected only to report AlreadyConnected. Disabled/not-present
    // endpoints are never enumerated, activated or used for reconnect requests.
    error=enumerator->EnumAudioEndpoints(eRender,DEVICE_STATE_ACTIVE|DEVICE_STATE_UNPLUGGED,&endpoints);
    if(FAILED(error))return {Status::Failed,error};
    UINT count=0;error=endpoints->GetCount(&count);if(FAILED(error))return {Status::Failed,error};
    if(count>kMaxEndpoints)return {Status::Unsupported,HRESULT_FROM_WIN32(ERROR_MORE_DATA)};
    std::vector<Candidate> candidates;
    Failures failures;
    for(UINT index=0;index<count;++index){
        ComPtr<IMMDevice> endpoint;error=endpoints->Item(index,&endpoint);if(FAILED(error))continue;
        DWORD state=0;error=endpoint->GetState(&state);if(FAILED(error))continue;
        if(state!=DEVICE_STATE_ACTIVE&&state!=DEVICE_STATE_UNPLUGGED)continue;
        bool matchedEndpoint=false;
        if(CollectCandidates(enumerator.Get(),endpoint.Get(),target,candidates,error,&matchedEndpoint)&&state==DEVICE_STATE_ACTIVE)
            return {Status::AlreadyConnected,S_OK};
        if(matchedEndpoint&&FAILED(error))failures.Observe(error);
    }
    for(const auto& candidate:candidates){
        DWORD state=0;error=candidate.endpoint->GetState(&state);if(FAILED(error)){failures.Observe(error);continue;}
        if(state==DEVICE_STATE_ACTIVE)return {Status::AlreadyConnected,S_OK};
        if(state!=DEVICE_STATE_UNPLUGGED){failures.Observe(state==DEVICE_STATE_DISABLED?kUnsupported:kNotReady);continue;}
        if(!EndpointMatchesTarget(candidate.endpoint.Get(),target,error)||!MatchesTarget(candidate.filter.Get(),target,error)){
            failures.Observe(error==S_FALSE?kUnsupported:error);continue;
        }
        ComPtr<IKsControl> control;
        error=candidate.filter->Activate(__uuidof(IKsControl),CLSCTX_INPROC_SERVER,nullptr,
                                         reinterpret_cast<void**>(control.GetAddressOf()));
        if(FAILED(error)){failures.Observe(error);continue;}
        KSPROPERTY property{};property.Set=KSPROPSETID_BtAudio;
        property.Id=KSPROPERTY_ONESHOT_RECONNECT;property.Flags=KSPROPERTY_TYPE_BASICSUPPORT;
        ULONG support=0,returned=0;
        error=control->KsProperty(&property,sizeof(property),&support,sizeof(support),&returned);
        if(FAILED(error)){failures.Observe(error);continue;}
        if(returned<sizeof(support)){failures.Observe(HRESULT_FROM_WIN32(ERROR_INVALID_DATA));continue;}
        if(!(support&KSPROPERTY_TYPE_GET)){failures.Observe(kUnsupported);continue;}
        const Result currentPair=ValidatePair(address);if(currentPair.status!=Status::Requested)return currentPair;
        Target currentTarget;
        if(!ResolveTarget(address,currentTarget,error))return Unavailable(error);
        if(!IsEqualGUID(currentTarget.container,target.container))return {Status::Unsupported,kUnsupported};
        if(!EndpointMatchesTarget(candidate.endpoint.Get(),target,error)||!MatchesTarget(candidate.filter.Get(),target,error)){
            failures.Observe(error==S_FALSE?kUnsupported:error);continue;
        }
        error=candidate.endpoint->GetState(&state);if(FAILED(error))return {Status::Failed,error};
        if(state==DEVICE_STATE_ACTIVE)return {Status::AlreadyConnected,S_OK};
        if(state!=DEVICE_STATE_UNPLUGGED){failures.Observe(state==DEVICE_STATE_DISABLED?kUnsupported:kNotReady);continue;}
        property.Flags=KSPROPERTY_TYPE_GET;returned=0;
        // Official filter-target property: GET with no payload, never SET.
        error=control->KsProperty(&property,sizeof(property),nullptr,0,&returned);
        return {SUCCEEDED(error)?Status::Requested:Status::Failed,error};
    }
    return Unavailable(failures.Error());
}
} // namespace

Result Request(uint64_t pairedAddress){
    try{return RequestImpl(pairedAddress);}
    catch(const std::bad_alloc&){return {Status::Failed,E_OUTOFMEMORY};}
    catch(...){return {Status::Failed,E_FAIL};}
}

} // namespace HeadphoneAudioReconnect
