#pragma once
#include <atomic>
#include <cstdint>
#include <new>
#include <optional>
#include <windows.h>

namespace PenHoverChannel {
inline constexpr wchar_t kName[]=L"Global\\OpenEGoHubPenHover";
struct Payload {
    std::atomic<uint32_t> version{0};
    std::atomic<uint32_t> enabled{1};
};
static_assert(std::atomic<uint32_t>::is_always_lock_free);
// Only the SYSTEM supervisor creates/writes this channel. The SYSTEM driver
// opens it read-only; UI commands still go through the existing tray channel.
class Writer {
    HANDLE m_mapping=nullptr;
    Payload* m_view=nullptr;
public:
    Writer()=default;
    Writer(const Writer&)=delete;
    Writer& operator=(const Writer&)=delete;
    ~Writer(){Close();}
    void Close(){if(m_view)UnmapViewOfFile(m_view);if(m_mapping)CloseHandle(m_mapping);m_view=nullptr;m_mapping=nullptr;}
    bool Open(bool enabled,const wchar_t* name=kName){
        Close();
        m_mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(Payload),name);
        if(!m_mapping)return false;
        const bool created=GetLastError()!=ERROR_ALREADY_EXISTS;
        m_view=static_cast<Payload*>(MapViewOfFile(m_mapping,FILE_MAP_READ|FILE_MAP_WRITE,0,0,sizeof(Payload)));
        if(!m_view){Close();return false;}
        if(created)new(m_view)Payload;
        else if(m_view->version.load(std::memory_order_acquire)!=1){Close();return false;}
        Set(enabled);m_view->version.store(1,std::memory_order_release);return true;
    }
    bool IsOpen() const{return m_view!=nullptr;}
    void Set(bool value){if(m_view)m_view->enabled.store(value?1u:0u,std::memory_order_release);}
    bool Value() const{return !m_view||m_view->enabled.load(std::memory_order_acquire)!=0;}
};
class Reader {
    HANDLE m_mapping=nullptr;
    const Payload* m_view=nullptr;
public:
    Reader()=default;
    Reader(const Reader&)=delete;
    Reader& operator=(const Reader&)=delete;
    ~Reader(){Close();}
    void Close(){if(m_view)UnmapViewOfFile(m_view);if(m_mapping)CloseHandle(m_mapping);m_view=nullptr;m_mapping=nullptr;}
    std::optional<bool> Read(const wchar_t* name=kName){
        if(!m_view){
            m_mapping=OpenFileMappingW(FILE_MAP_READ,FALSE,name);
            if(!m_mapping)return std::nullopt;
            m_view=static_cast<const Payload*>(MapViewOfFile(m_mapping,FILE_MAP_READ,0,0,sizeof(Payload)));
            if(!m_view){Close();return std::nullopt;}
        }
        if(m_view->version.load(std::memory_order_acquire)!=1)return std::nullopt;
        const uint32_t value=m_view->enabled.load(std::memory_order_acquire);
        if(value>1)return std::nullopt;
        return value!=0;
    }
};
}
