#include "TouchProviderCoordinator.h"
#include <algorithm>
#include <utility>
namespace Service {
TouchProviderCoordinator::TouchProviderCoordinator(TouchProviderOperations operations,
        StateChanged changed,std::chrono::milliseconds)
    :m_operations(std::move(operations)),m_stateChanged(std::move(changed)){}
void TouchProviderCoordinator::Publish(PenStatus::TouchProviderState state,TouchProviderError error){
    m_state=state;m_error=error;if(m_stateChanged)m_stateChanged(state,error);
}
TouchProviderCoordinator::Clock::time_point TouchProviderCoordinator::ReadClock() const{
    return m_operations.now?m_operations.now():Clock::now();
}
void TouchProviderCoordinator::Failed(Clock::time_point now,TouchProviderError error){
    ++m_failedAttempts;
    if(m_failedAttempts>kMaxRetries){
        m_exhausted=true;m_retryScheduled=false;
        Publish(PenStatus::TouchProviderState::Error,TouchProviderError::RetriesExhausted);
    }else{
        m_retryScheduled=true;
        m_nextAttempt=std::max(now,ReadClock())+kRetryDelay;
        Publish(PenStatus::TouchProviderState::Error,error);
    }
}
bool TouchProviderCoordinator::AttemptStart(Clock::time_point now){
    if(m_exhausted||m_suspended||!m_started)return false;
    m_retryScheduled=false;
    Publish(PenStatus::TouchProviderState::SwitchingToEGo);
    // Disable before starting, even when input startup subsequently fails.
    if(!m_operations.disableHuawei||!m_operations.disableHuawei()){
        Failed(now,TouchProviderError::DisableHuaweiFailed);return false;
    }
    if(!m_operations.stopHuawei||!m_operations.stopHuawei()){
        Failed(now,TouchProviderError::StopHuaweiFailed);return false;
    }
    if(!m_operations.stopEGo||!m_operations.stopEGo()){
        Failed(now,TouchProviderError::StopEGoFailed);return false;
    }
    if(!m_operations.startEGo||!m_operations.startEGo()){
        Failed(now,TouchProviderError::StartEGoFailed);return false;
    }
    m_failedAttempts=0;
    m_nextOemAudit=std::max(now,ReadClock())+kOemAuditInterval;
    Publish(PenStatus::TouchProviderState::EGoTouch);return true;
}
bool TouchProviderCoordinator::AcquireOrRenew(Clock::time_point now){
    m_hasLease=true;
    if(!m_started){m_started=true;return AttemptStart(now);}
    // Automatic tray renewals must not reset or restart an exhausted batch.
    return !m_exhausted;
}
bool TouchProviderCoordinator::Release(){m_hasLease=false;return true;}
void TouchProviderCoordinator::Tick(Clock::time_point now){
    if(!m_started)return;
    if(now>=m_nextOemAudit){
        m_nextOemAudit=std::max(now,ReadClock())+kOemAuditInterval;
        const bool disabled=m_operations.disableHuawei&&m_operations.disableHuawei();
        if(!disabled&&!m_exhausted&&!m_suspended&&!m_retryScheduled){
            if(m_operations.stopEGo)(void)m_operations.stopEGo();
            Failed(now,TouchProviderError::DisableHuaweiFailed);return;
        }
    }
    if(m_suspended||m_exhausted)return;
    if(m_retryScheduled){if(now>=m_nextAttempt)(void)AttemptStart(now);return;}
    if(m_state==PenStatus::TouchProviderState::EGoTouch&&
       m_operations.egoAlive&&!m_operations.egoAlive())(void)AttemptStart(now);
}
void TouchProviderCoordinator::OnSuspend(Clock::time_point){
    if(!m_started||m_suspended)return;
    m_suspended=true;
    const bool stopped=m_operations.stopEGo&&m_operations.stopEGo();
    if(!m_exhausted)Publish(stopped?PenStatus::TouchProviderState::EGoSuspended:
        PenStatus::TouchProviderState::Error,stopped?TouchProviderError::None:TouchProviderError::StopEGoFailed);
}
void TouchProviderCoordinator::OnResume(Clock::time_point now){
    if(!m_suspended)return;
    m_suspended=false;
    if(!m_exhausted)(void)AttemptStart(now);
}
void TouchProviderCoordinator::Shutdown(bool){
    m_started=false;m_hasLease=false;m_suspended=false;m_retryScheduled=false;
    if(m_operations.stopEGo)(void)m_operations.stopEGo();
    if(m_operations.disableHuawei)(void)m_operations.disableHuawei();
}
}
