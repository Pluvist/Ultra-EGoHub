#pragma once
#include <cstdint>
#include <optional>

namespace KeyboardAttachment {
// Unknown data never means "detached". Startup/recovery only establishes
// a baseline, and suppressed edges are consumed rather than replayed later.
class Edge {
public:
    void Reset() noexcept { m_previous.reset(); }
    bool Observe(bool providerHealthy,std::optional<bool> attached,uint64_t now,bool enabled) noexcept {
        if(!providerHealthy){Reset();return false;}
        if(!attached)return false;
        const bool rising=m_previous.has_value()&&!*m_previous&&*attached;
        m_previous=attached;
        if(!rising||!enabled)return false;
        if(m_lastPlayed&&(now<*m_lastPlayed||now-*m_lastPlayed<1000))return false;
        m_lastPlayed=now;return true;
    }
private:
    std::optional<bool> m_previous;
    std::optional<uint64_t> m_lastPlayed;
};
}
