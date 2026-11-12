#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace HeadphoneConnections {

inline bool IsHuaweiHeadphoneName(std::wstring_view name) {
    if(name.find(L"华为")!=std::wstring_view::npos &&
       (name.find(L"耳机")!=std::wstring_view::npos || name.find(L"眼镜")!=std::wstring_view::npos))return true;
    std::wstring normalized;
    for(wchar_t c:name){
        if(c>=L'A'&&c<=L'Z')c=static_cast<wchar_t>(c-L'A'+L'a');
        if((c>=L'a'&&c<=L'z')||(c>=L'0'&&c<=L'9'))normalized+=c;
    }
    if(normalized.starts_with(L"huawei"))normalized.erase(0,6);
    return normalized.starts_with(L"freebuds")||normalized.starts_with(L"freeclip")||
        normalized.starts_with(L"freearc")||normalized.starts_with(L"eyewear");
}

// Monotonic milliseconds, injected by the monitor. A short ACL interruption is
// kept as the same connection so normal radio/profile churn does not re-toast.
class Edges {
public:
    static constexpr uint64_t kDisconnectGraceMs=3000;
    static constexpr uint64_t kAclSnapshotGraceMs=5000;
    bool ObserveAcl(uint64_t address,bool connected,uint64_t now){
        const bool notify=Observe(address,connected,now);
        if(auto found=m_entries.find(address);found!=m_entries.end())
            found->second.authority=Authority{connected,now};
        return notify;
    }
    bool ObserveSnapshot(uint64_t address,bool connected,uint64_t now,bool baseline=false){
        Advance(now);
        if(!baseline){
            if(auto found=m_entries.find(address);found!=m_entries.end()&&found->second.authority){
                const auto& authority=*found->second.authority;
                // A delayed cache must neither undo a new ACL connection nor
                // cancel the disconnect grace after a real ACL disconnection.
                if(connected!=authority.connected&&now>=authority.observedAt&&
                   now-authority.observedAt<kAclSnapshotGraceMs)return false;
            }
        }
        const bool notify=Observe(address,connected,now,baseline);
        if(baseline)if(auto found=m_entries.find(address);found!=m_entries.end())found->second.authority.reset();
        return notify;
    }
    bool Contains(uint64_t address) const{return m_entries.contains(address);}
    bool IsConnected(uint64_t address) const{
        auto found=m_entries.find(address);return found!=m_entries.end()&&found->second.connected;
    }
    bool DisconnectPending(uint64_t address) const{
        auto found=m_entries.find(address);return found!=m_entries.end()&&found->second.disconnectedAt.has_value();
    }
private:
    bool Observe(uint64_t address,bool connected,uint64_t now,bool baseline=false){
        if(address==0 || address>0xffffffffffffULL)return false;
        Advance(now);
        if(!m_entries.contains(address) && m_entries.size()>=128){
            auto retired=std::find_if(m_entries.begin(),m_entries.end(),[](auto const& item){return !item.second.connected;});
            if(retired==m_entries.end())return false;
            m_entries.erase(retired);
        }
        auto& state=m_entries[address];
        if(connected){
            bool notify=!state.connected&&!baseline;
            state.connected=true;state.disconnectedAt.reset();return notify;
        }
        if(baseline){state.connected=false;state.disconnectedAt.reset();}
        else if(state.connected&&!state.disconnectedAt)state.disconnectedAt=now;
        return false;
    }
public:
    void Advance(uint64_t now){
        for(auto& [address,state]:m_entries){
            if(state.disconnectedAt && now>=*state.disconnectedAt && now-*state.disconnectedAt>=kDisconnectGraceMs){
                state.connected=false;state.disconnectedAt.reset();
            }
        }
    }
    std::optional<uint64_t> Deadline() const {
        std::optional<uint64_t> next;
        for(auto const& [address,state]:m_entries)if(state.disconnectedAt){
            auto when=*state.disconnectedAt+kDisconnectGraceMs;
            if(!next||when<*next)next=when;
        }
        return next;
    }
    void Reset(){m_entries.clear();}
private:
    struct Authority{bool connected;uint64_t observedAt;};
    struct State{
        bool connected=false;
        std::optional<uint64_t> disconnectedAt;
        std::optional<Authority> authority;
    };
    std::map<uint64_t,State> m_entries;
};

// Only unresolved ACL events use this short retry schedule. A duplicate event
// never extends the original deadline, and idle monitoring keeps its 15s tick.
class PendingConnections {
public:
    static constexpr uint64_t kRetryMs=250;
    static constexpr uint64_t kTimeoutMs=2000;
    static constexpr size_t kMaxEntries=32;
    bool Add(uint64_t address,uint64_t now){
        if(!address||address>0xffffffffffffULL)return false;
        if(m_entries.contains(address))return true;
        if(m_entries.size()>=kMaxEntries)return false;
        m_entries.emplace(address,Entry{now+kRetryMs,now+kTimeoutMs});
        return true;
    }
    void Remove(uint64_t address){m_entries.erase(address);}
    std::optional<uint64_t> Deadline() const{
        std::optional<uint64_t> next;
        for(const auto& [address,entry]:m_entries){
            const auto when=std::min(entry.nextAttempt,entry.expiresAt);
            if(!next||when<*next)next=when;
        }
        return next;
    }
    std::vector<uint64_t> Due(uint64_t now){
        std::vector<uint64_t> due;
        for(auto it=m_entries.begin();it!=m_entries.end();){
            auto& entry=it->second;
            if(now>=entry.expiresAt){it=m_entries.erase(it);continue;}
            if(now>=entry.nextAttempt){
                due.push_back(it->first);
                entry.nextAttempt=std::min(now+kRetryMs,entry.expiresAt);
            }
            ++it;
        }
        return due;
    }
private:
    struct Entry{uint64_t nextAttempt;uint64_t expiresAt;};
    std::map<uint64_t,Entry> m_entries;
};

// Device-change hints can precede the Bluetooth cache update. Probe only this
// finite burst; repeated hints neither extend it nor create an idle poll loop.
class CacheProbeSchedule {
public:
    bool Hint(uint64_t now){
        if(m_started)return false;
        m_started=now;m_next=0;return true;
    }
    std::optional<uint64_t> Deadline() const{
        if(!m_started)return {};
        return *m_started+kOffsets[m_next];
    }
    bool Due(uint64_t now){
        if(!m_started||now<*m_started+kOffsets[m_next])return false;
        // If the worker was delayed, one scan handles all elapsed points.
        do{++m_next;}while(m_next<kOffsets.size()&&now>=*m_started+kOffsets[m_next]);
        if(m_next==kOffsets.size()){m_started.reset();m_next=0;}
        return true;
    }
private:
    static constexpr std::array<uint64_t,5> kOffsets{0,100,500,1200,2500};
    std::optional<uint64_t> m_started;
    size_t m_next=0;
};

// Reconnect requests are attempts, never connection evidence. Explicit ACL
// disconnects can retry before the independent notification debounce expires;
// cache-only disconnects still require the effective, protected Edges state.
class ReconnectSchedule {
public:
    static constexpr uint64_t kDisconnectDelayMs=250;
    static constexpr std::array<uint64_t,6> kDisconnectOffsets{0,1000,3000,7000,15000,30000};
    static constexpr uint64_t kPresenceSilenceMs=5000;
    void SetEnabled(bool enabled){
        if(m_enabled==enabled)return;
        m_enabled=enabled;CancelRound();
        m_cacheDisconnectMayRetry=false;
    }
    void Connected(uint64_t address,uint64_t,bool baseline=false){
        if(!address||address>0xffffffffffffULL||(baseline&&m_target))return;
        const bool newConnection=address!=m_target||!m_transportConnected;
        if(!newConnection)return;
        m_target=address;m_transportConnected=true;m_debouncedConnected=true;m_eligible=true;
        m_cacheDisconnectPending=false;m_cacheDisconnectMayRetry=false;
        m_lastPresence.reset();m_presenceConsumed=false;
        CancelRound();
    }
    void RestoreDisconnected(uint64_t address){
        if(m_target||!address||address>0xffffffffffffULL)return;
        m_target=address;m_eligible=true;
        // A saved target is not a live connection or a reason to issue a page.
    }
    uint64_t PresenceTarget() const{return m_enabled&&m_eligible&&!m_transportConnected?m_target:0;}
    bool PresenceHint(uint64_t address,uint64_t now){
        if(!address||address!=m_target)return false;
        const bool newEpisode=!m_lastPresence||
            (now>=*m_lastPresence&&now-*m_lastPresence>=kPresenceSilenceMs);
        m_lastPresence=now; // Every match counts, including matches during a round.
        if(newEpisode)m_presenceConsumed=false;
        if(!m_enabled||!m_eligible||m_transportConnected)return false;
        if(m_roundAt){m_presenceConsumed=true;return false;}
        if(m_presenceConsumed)return false;
        m_presenceConsumed=true;m_roundAt=now+kDisconnectDelayMs;m_next=0;
        return true;
    }
    void Disconnected(uint64_t address,uint64_t now){
        if(address!=m_target)return;
        MarkDisconnected(now,true);
    }
    uint64_t Target() const{return m_target;}
    void ObserveTarget(bool connected,bool disconnectPending,uint64_t now){
        if(!m_target)return;
        if(connected){
            if(disconnectPending){
                if(!m_cacheDisconnectPending){
                    m_cacheDisconnectPending=true;m_cacheDisconnectMayRetry=m_enabled;
                }
            }else{
                m_cacheDisconnectPending=false;m_cacheDisconnectMayRetry=false;
            }
            // During the notification grace, Edges can remain connected after
            // a real ACL false. It must not cancel a fast disconnect round.
            m_debouncedConnected=true;
            return;
        }
        if(m_debouncedConnected){
            m_debouncedConnected=false;
            MarkDisconnected(now,!m_cacheDisconnectPending||m_cacheDisconnectMayRetry);
        }
        m_cacheDisconnectPending=false;m_cacheDisconnectMayRetry=false;
    }
    std::optional<uint64_t> Deadline() const{
        if(!m_enabled||!m_roundAt)return {};
        return *m_roundAt+kDisconnectOffsets[m_next];
    }
    std::optional<uint64_t> TakeDue(uint64_t now){
        const auto deadline=Deadline();
        if(!deadline||now<*deadline)return {};
        const auto target=m_target;
        // Never issue a burst of overdue connection attempts after a slow API.
        const auto& offsets=kDisconnectOffsets;
        do{++m_next;}while(m_next<offsets.size()&&now>=*m_roundAt+offsets[m_next]);
        if(m_next==offsets.size())CancelRound();
        return target;
    }
    void CancelRound(){m_roundAt.reset();m_next=0;}
    void ManualOverride(uint64_t address,uint64_t now){
        CancelRound();m_cacheDisconnectMayRetry=false;
        m_lastPresence=now;m_presenceConsumed=true;
        if(address!=m_target)m_eligible=false;
    }
private:
    void MarkDisconnected(uint64_t now,bool mayRetry){
        if(!m_transportConnected)return;
        m_transportConnected=false;CancelRound();
        if(m_enabled&&m_eligible&&mayRetry)m_roundAt=now+kDisconnectDelayMs;
    }
    uint64_t m_target=0;
    bool m_enabled=false,m_transportConnected=false,m_debouncedConnected=false,m_eligible=false;
    bool m_cacheDisconnectPending=false,m_cacheDisconnectMayRetry=false;
    bool m_presenceConsumed=false;
    std::optional<uint64_t> m_roundAt,m_lastPresence;
    size_t m_next=0;
};
}
