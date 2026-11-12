#pragma once
#include <windows.h>
#include <shlobj.h>
#include <cstdio>
#include <algorithm>
#include <memory>
#include <string>
#include <string_view>

// Opt-in, bounded local diagnostics. Never includes names, addresses or values.
// Remove Cache/trace-popup to stop immediately; normal installs have no marker.
namespace PopupTimingTrace {
struct Paths { std::wstring marker, log; };
inline const Paths& Files() {
    static const Paths paths=[] {
        wchar_t local[MAX_PATH]{};
        if(FAILED(SHGetFolderPathW(nullptr,CSIDL_LOCAL_APPDATA,nullptr,0,local)))return Paths{};
        const auto root=std::wstring(local)+L"\\OpenEGoHub\\Cache\\";
        return Paths{root+L"trace-popup",root+L"popup-timing.log"};
    }();
    return paths;
}
struct Entry { std::wstring file;std::string line; };
inline DWORD CALLBACK Write(void* parameter) noexcept {
    std::unique_ptr<Entry> entry(static_cast<Entry*>(parameter));
    const auto file=CreateFileW(entry->file.c_str(),FILE_APPEND_DATA|GENERIC_READ,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return 0;
    LARGE_INTEGER size{};
    if(GetFileSizeEx(file,&size)&&size.QuadPart<128*1024){
        DWORD written=0;WriteFile(file,entry->line.data(),static_cast<DWORD>(entry->line.size()),&written,nullptr);
    }
    CloseHandle(file);return 0;
}
inline void Record(std::string_view stage,int detail=0) noexcept {
    try{
        const auto& files=Files();
        if(files.marker.empty()||GetFileAttributesW(files.marker.c_str())==INVALID_FILE_ATTRIBUTES)return;
        char line[160]{};
        const auto count=std::snprintf(line,sizeof(line),"%llu %lu %.*s %d\r\n",
            static_cast<unsigned long long>(GetTickCount64()),GetCurrentProcessId(),
            static_cast<int>((std::min)(stage.size(),size_t{64})),stage.data(),detail);
        if(count<=0||static_cast<size_t>(count)>=sizeof(line))return;
        auto entry=std::make_unique<Entry>(Entry{files.log,std::string(line,static_cast<size_t>(count))});
        if(QueueUserWorkItem(Write,entry.get(),WT_EXECUTEDEFAULT))entry.release();
    }catch(...){}
}
}
