#pragma once
#include "PenStatusChannel.h"
#include <chrono>
#include <cstdint>
#include <functional>

namespace Service {
// Keep existing numeric values for the status-channel ABI.
enum class TouchProviderError : uint8_t {
    None=0, StopHuaweiFailed, StartEGoFailed, DisableHuaweiFailed,
    StopEGoFailed, RestoreHuaweiFailed, RollbackEGoFailed, EGoHostDied,
    RetriesExhausted,
};
struct TouchProviderOperations {
    std::function<bool()> stopHuawei;
    std::function<bool()> disableHuawei;
    std::function<bool()> startEGo;
    std::function<bool()> stopEGo;
    std::function<bool()> egoAlive;
    std::function<std::chrono::steady_clock::time_point()> now;
};
// The service owns input independently of the tray. OEM input is never restored.
class TouchProviderCoordinator {
public:
    using Clock=std::chrono::steady_clock;
    using StateChanged=std::function<void(PenStatus::TouchProviderState,TouchProviderError)>;
    static constexpr int kMaxRetries=5;
    static constexpr std::chrono::seconds kRetryDelay{2};
    static constexpr std::chrono::seconds kOemAuditInterval{5};
    TouchProviderCoordinator(TouchProviderOperations,StateChanged,std::chrono::milliseconds);
    bool AcquireOrRenew(Clock::time_point);
    bool Release(); // legacy tray command: input continues running
    void Tick(Clock::time_point);
    void OnSuspend(Clock::time_point);
    void OnResume(Clock::time_point);
    void Shutdown(bool systemShutdown);
    auto State() const noexcept {return m_state;}
    auto Error() const noexcept {return m_error;}
    bool HasLease() const noexcept {return m_hasLease;}
    bool IsSuspended() const noexcept {return m_suspended;}
private:
    void Publish(PenStatus::TouchProviderState,TouchProviderError=TouchProviderError::None);
    Clock::time_point ReadClock() const;
    bool AttemptStart(Clock::time_point);
    void Failed(Clock::time_point,TouchProviderError);
    TouchProviderOperations m_operations;
    StateChanged m_stateChanged;
    PenStatus::TouchProviderState m_state=PenStatus::TouchProviderState::Unknown;
    TouchProviderError m_error=TouchProviderError::None;
    Clock::time_point m_nextAttempt{},m_nextOemAudit{};
    int m_failedAttempts=0;
    bool m_started=false,m_hasLease=false,m_suspended=false;
    bool m_retryScheduled=false,m_exhausted=false;
};
}
