#pragma once

#include "btmcu/PenUsbTypes.h"

namespace Himax::Pen {

enum class PenUsbInitAction : uint8_t {
    None = 0,
    SendInitialQueries,
    SendSecondMcuStatusQuery,
    SendFactoryInitProtocolParams,
};

class PenUsbInitSession {
public:
    PenUsbInitAction OnConnected() noexcept {
        m_phase = Phase::AwaitingMcuStatus;
        return PenUsbInitAction::SendInitialQueries;
    }

    bool IsHandshakeComplete() const noexcept { return m_phase == Phase::Running; }

    void OnInitParamsSent(bool sent) noexcept {
        if (m_phase == Phase::SendingInitParams) {
            m_phase = sent ? Phase::Running : Phase::AwaitingPenStatus;
        }
    }

    PenUsbInitAction OnEvent(PenUsbEventCode code) noexcept {
        switch (m_phase) {
        case Phase::AwaitingPenStatus:
            return PenUsbInitAction::None;

        case Phase::AwaitingMcuStatus:
            if (code == PenUsbEventCode::PenScreenStatus) {
                m_phase = Phase::AwaitingInitParamRequest;
                return PenUsbInitAction::SendSecondMcuStatusQuery;
            }
            // The MCU can request the parameters before a screen-status
            // reply reaches this channel. Its explicit request is sufficient.
            [[fallthrough]];

        case Phase::AwaitingInitParamRequest:
            if (code == PenUsbEventCode::PenRepParam) {
                m_phase = Phase::SendingInitParams;
                return PenUsbInitAction::SendFactoryInitProtocolParams;
            }
            return PenUsbInitAction::None;

        case Phase::SendingInitParams:
        case Phase::Running:
            return PenUsbInitAction::None;
        }
        return PenUsbInitAction::None;
    }

private:
    enum class Phase : uint8_t {
        AwaitingPenStatus = 0,
        AwaitingMcuStatus,
        AwaitingInitParamRequest,
        SendingInitParams,
        Running,
    };

    Phase m_phase = Phase::AwaitingPenStatus;
};

} // namespace Himax::Pen
