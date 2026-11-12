#include "TsaOutputMapper.h"
#include "TsaContactEvents.h"
#include "VhfTouchState.h"
#include "VhfStylusState.h"
#include "VhfReporterStylusPacketHelper.h"
#include "PenHoverPolicy.h"
#include <iostream>
#include <stdexcept>
#include <vector>

// Replay the actual mapper without loading hardware or the global TSACore.
static std::vector<Solvers::Tsa::Core::ReportedTouch> reports;
namespace Solvers::Tsa::Core {
uint8_t TouchCount() { return static_cast<uint8_t>(reports.size()); }
ReportedTouch TouchAt(uint8_t index) { return reports.at(index); }
}
static void Require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
static void ReplayTouchDrag() {
    Solvers::Tsa::TsaOutputMapper mapper;
    Solvers::FixedVector<Solvers::TouchContact, 20> contacts;
    reports = {{0, true, false, 1000, 2000, 50}};
    Require(mapper.Collect(contacts, true) == 1 && contacts[0].reportEvent == Solvers::TouchReportDown, "initial touch down");
    for (int i = 0; i < 500; ++i) {
        reports.clear(); // skipped / rejected master: no evidence of finger lift
        Require(mapper.Collect(contacts, false) == 0 && mapper.HasActiveContacts(), "missing matrix must not release the drag");
        const uint32_t event = i % 3 == 0 ? 4u : (i % 3 == 1 ? 8u : 16u);
        reports = {{0, Solvers::Tsa::ContactEvents::Down(event), Solvers::Tsa::ContactEvents::Lift(event),
                    static_cast<uint16_t>(1000 + i), 2000, 50}};
        mapper.Collect(contacts, true);
        Require(contacts.size() == 1 && contacts[0].id == 1 && contacts[0].reportEvent == Solvers::TouchReportMove, "resumed frame keeps ID and emits move");
    }
    const auto last = contacts[0];
    reports = {{0, false, true, 0, 0, 0}};
    mapper.Collect(contacts, true);
    Require(contacts[0].reportEvent == Solvers::TouchReportUp && contacts[0].x == last.x && contacts[0].y == last.y, "lift preserves last contact position");
    Require(!mapper.HasActiveContacts(), "explicit up releases immediately");
    reports = {{0, true, false, 1000, 2000, 50}};
    mapper.Collect(contacts, true);
    reports.clear();
    Require(mapper.Collect(contacts, true) == 1 && contacts[0].reportEvent == Solvers::TouchReportUp, "real empty master releases without debounce delay");
    mapper.Reset();
    for (int tap = 0; tap < 30; ++tap) {
        for (uint32_t event : {2u, 4u, 8u, 16u, 32u}) {
            reports = {{0, Solvers::Tsa::ContactEvents::Down(event), Solvers::Tsa::ContactEvents::Lift(event), 1000, 2000, 50}};
            mapper.Collect(contacts, true);
            Require(contacts.size() == 1, "short tap must report every lifecycle stage");
            Require(contacts[0].reportEvent == (event == 2 ? Solvers::TouchReportDown :
                (event == 32 ? Solvers::TouchReportUp : Solvers::TouchReportMove)), "tap moves/holds must not turn into premature lifts");
        }
        Require(!mapper.HasActiveContacts(), "short tap releases exactly once");
    }
    mapper.Reset();
    Require(!mapper.HasActiveContacts(), "restart resets contact tracking");
}
static void ReplayMultiTouchRelease() {
    Solvers::FixedVector<Solvers::TouchContact, 20> contacts;
    for (int i = 1; i <= 10; ++i) {
        Solvers::TouchContact c;
        c.id = i; c.x = float(i * 100); c.y = float(i * 70);
        c.reportEvent = Solvers::TouchReportDown;
        contacts.push_back(c);
    }
    const auto down = VhfTouchPacket::Build(contacts.span(), false);
    Require(down[0].bytes[31] == 10 && down[1].bytes[31] == 0, "ten contacts use a valid hybrid frame");
    VhfTouchState state;
    state.Observe(down, true);
    auto failedMove = down;
    failedMove[0].bytes[3] ^= 0x10;
    state.Observe(failedMove, false);
    const auto up = state.ReleasePackets();
    Require(up[0].bytes[31] == 10 && up[0].valid && up[1].valid, "flush explicitly reports all IDs");
    for (size_t i = 0; i < 10; ++i) {
        size_t p = i / 5, b = 1 + (i % 5) * 6;
        Require(up[p].bytes[b] == 2, "flush clears tip switch");
        for (size_t j = 1; j < 6; ++j) Require(up[p].bytes[b+j] == down[p].bytes[b+j], "flush preserves ID and encoded position");
    }
    state.Observe(up, false);
    Require(state.HasActive(), "failed release remains pending for reopen");
    state.Observe(up, true);
    Require(!state.HasActive(), "successful release clears pending contacts");
    state.Observe(up, true);
    Require(!state.HasActive(), "duplicate release keeps the active count at zero");
    // A partly delivered hybrid frame must retain possible downs for cleanup.
    state.Observe(down, false);
    Require(state.HasActive() && state.ReleasePackets()[0].bytes[31] == 10, "partial write retains cleanup state");
}
static void ReplayPenDrag() {
    Solvers::StylusFrameData stylus;
    stylus.output.valid = stylus.output.inRange = stylus.output.tipDown = true;
    stylus.output.point.valid = true;
    stylus.output.pressure = 900;
    for (int i = 0; i < 500; ++i) {
        stylus.output.point.x = float(3000 + i * 10);
        stylus.output.point.y = 5000;
        auto packet = VhfStylusPacket::Build(stylus, {});
        auto bytes = packet.bytes;
        VhfStylusState::ApplyTool(bytes, false);
        PenHoverPolicy::Apply(bytes, false, true);
        Require(bytes == packet.bytes && (bytes[1] & 1u), "contact motion/pressure untouched with hover off");
    }
    std::array<uint8_t, 17> hover{8, 0x20};
    VhfStylusState::ApplyTool(hover, true);
    Require(hover[1] == 0x24, "eraser hover is inverted, not touching");
    auto ink = VhfStylusPacket::Build(stylus, {}).bytes;
    VhfStylusState::ApplyTool(ink, true);
    Require(ink[1] == 0x2c, "eraser contact is preserved");
    std::array<uint8_t, 17> neutral{8};
    VhfStylusState::ApplyTool(neutral, true);
    Require(neutral[1] == 0, "eraser selection must not resurrect an absent pen");
    VhfStylusState::KeepReleasePosition(neutral, ink);
    for (size_t i = 3; i < 7; ++i) Require(neutral[i] == ink[i], "pen exit keeps last successful contact position");
    Require(neutral[1] == 0 && neutral[7] == 0 && neutral[8] == 0, "pen exit clears contact and pressure");
}
int main() {
    try {
        ReplayTouchDrag(); ReplayMultiTouchRelease(); ReplayPenDrag();
        std::cout << "Drag continuity, ten-contact release, pen motion and tool-state replay passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
