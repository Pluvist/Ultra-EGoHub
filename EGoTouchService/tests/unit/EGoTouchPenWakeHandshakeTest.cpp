#include "btmcu/PenUsbInitSession.h"

int main() {
    using Himax::Pen::PenUsbEventCode;
    using Himax::Pen::PenUsbInitAction;
    Himax::Pen::PenUsbInitSession session;

    // A queued pre-wake reply must not advance a fresh channel.
    if (session.OnEvent(PenUsbEventCode::PenRepParam) != PenUsbInitAction::None) return 1;
    if (session.OnConnected() != PenUsbInitAction::SendInitialQueries) return 2;
    if (session.OnEvent(PenUsbEventCode::PenScreenStatus) !=
        PenUsbInitAction::SendSecondMcuStatusQuery) return 3;
    if (session.OnEvent(PenUsbEventCode::PenRepParam) !=
        PenUsbInitAction::SendFactoryInitProtocolParams) return 4;
    if (session.IsHandshakeComplete()) return 5;
    session.OnInitParamsSent(false);
    if (session.IsHandshakeComplete()) return 6;
    if (session.OnEvent(PenUsbEventCode::PenRepParam) != PenUsbInitAction::None) return 7;

    if (session.OnConnected() != PenUsbInitAction::SendInitialQueries) return 8;
    // A genuine MCU parameter request may precede the screen-status reply.
    if (session.OnEvent(PenUsbEventCode::PenRepParam) !=
        PenUsbInitAction::SendFactoryInitProtocolParams) return 10;
    session.OnInitParamsSent(true);
    if (!session.IsHandshakeComplete()) return 11;
    if (session.OnEvent(PenUsbEventCode::PenRepParam) != PenUsbInitAction::None) return 12;
    return 0;
}
