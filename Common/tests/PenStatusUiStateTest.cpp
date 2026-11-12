#include "PenStatusUiState.h"

#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

void Check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

template<typename Change>
void CheckChange(const PenStatus::State& baseline, Change change, const char* message) {
    auto changed = baseline;
    change(changed);
    Check(!PenStatus::SameUiState(baseline, changed), message);
    Check(!PenStatus::SameUiState(changed, baseline), message);
}

void CheckTimestamps() {
    PenStatus::State original;
    original.updatedAtUnixMs = 100;
    auto newer = original;
    newer.updatedAtUnixMs = (std::numeric_limits<uint64_t>::max)();
    Check(PenStatus::SameUiState(original, newer), "publish time alone is not a UI change");
    Check(PenStatus::SameUiState(newer, original), "timestamp comparison is symmetric");
    Check(original.updatedAtUnixMs == 100 &&
          newer.updatedAtUnixMs == (std::numeric_limits<uint64_t>::max)(),
          "UI comparison must not modify snapshot freshness");
    Check(!(original == newer), "full State equality retains the publish timestamp");
    newer.updatedAtUnixMs = 0;
    Check(PenStatus::SameUiState(original, newer), "an unset publish time is ignored too");
}

void CheckSemanticChanges() {
    using PenStatus::State;
    State baseline;
    baseline.updatedAtUnixMs = 1234;
    Check(PenStatus::SameUiState(baseline, baseline), "identical state compares equal");

    struct Flag {
        const char* message;
        bool State::* member;
    };
    const Flag flags[]{
        {"battery availability changed", &State::hasBatteryLevel},
        {"charging availability changed", &State::hasChargingState},
        {"charging changed", &State::charging},
        {"attachment availability changed", &State::hasDeviceAttached},
        {"attachment changed", &State::deviceAttached},
        {"stylus link availability changed", &State::hasStylusLink},
        {"stylus link changed", &State::stylusLinked},
        {"hover availability changed", &State::hasPenHoverEnabled},
        {"hover changed", &State::penHoverEnabled},
        {"pen button mode availability changed", &State::hasPenButtonMode},
        {"eraser availability changed", &State::hasEraserActive},
        {"eraser changed", &State::eraserActive},
        {"provider availability changed", &State::hasTouchProvider},
        {"provider error availability changed", &State::hasProviderError},
        {"input suppression availability changed", &State::hasInputSuppressed},
        {"input suppression changed", &State::inputSuppressed},
        {"keyboard wireless availability changed", &State::hasKbdDetachSupport},
        {"keyboard wireless changed", &State::kbdDetachSupport},
        {"host health availability changed", &State::hasHostHealth},
        {"pen host health changed", &State::penHostHealthy},
        {"keyboard host health changed", &State::kbdHostHealthy},
        {"pen vendor component availability changed", &State::penVendorMissing},
        {"keyboard vendor component availability changed", &State::kbdVendorMissing},
        {"vendor service availability changed", &State::hasVendorServices},
        {"vendor service disabled state changed", &State::vendorServicesDisabled},
        {"vendor service running state changed", &State::vendorServicesRunning},
        {"vendor service completion changed", &State::vendorServicesAllRunning},
        {"charge limit availability changed", &State::hasChargeLimit},
        {"charge limit mode changed", &State::chargeLimitManual},
        {"pen firmware availability changed", &State::hasPenFirmware},
        {"pen hardware availability changed", &State::hasPenHardware},
        {"pen serial availability changed", &State::hasPenSerial},
        {"keyboard presence availability changed", &State::hasKbdPresent},
        {"keyboard presence changed", &State::kbdPresent},
        {"keyboard attachment availability changed", &State::hasKbdDetached},
        {"keyboard attachment changed", &State::kbdDetached},
        {"keyboard battery availability changed", &State::hasKbdBattery},
        {"keyboard charging availability changed", &State::hasKbdCharging},
        {"keyboard charging changed", &State::kbdCharging},
    };
    for (const auto& flag : flags) {
        CheckChange(baseline, [member = flag.member](State& state) {
            state.*member = !(state.*member);
        }, flag.message);
    }

    struct Byte {
        const char* message;
        uint8_t State::* member;
    };
    const Byte bytes[]{
        {"battery level changed", &State::batteryLevel},
        {"pen button mode changed", &State::penButtonMode},
        {"provider error changed", &State::providerError},
        {"charge limit changed", &State::chargeLimit},
        {"keyboard battery level changed", &State::kbdBatteryLevel},
        {"update major changed", &State::updateMajor},
        {"update minor changed", &State::updateMinor},
        {"update patch changed", &State::updatePatch},
    };
    for (const auto& byte : bytes) {
        CheckChange(baseline, [member = byte.member](State& state) {
            state.*member = 1;
        }, byte.message);
    }

    CheckChange(baseline, [](State& state) { state.notificationSequence = 1; },
                "a repeated notification edge remains visible to the caller");
    CheckChange(baseline, [](State& state) {
        state.notificationKind = PenStatus::NotificationKind::PenConnected;
    }, "notification kind changed");
    CheckChange(baseline, [](State& state) {
        state.touchProvider = PenStatus::TouchProviderState::EGoTouch;
    }, "provider state changed");
    CheckChange(baseline, [](State& state) {
        state.updateState = PenStatus::UpdateState::Available;
    }, "update state changed");
    CheckChange(baseline, [](State& state) { state.modelId = 282; },
                "accessory model identity changed");

    CheckChange(baseline, [](State& state) { state.modelName[0] = 'P'; },
                "pen model text changed");
    CheckChange(baseline, [](State& state) { state.penFirmware[0] = '1'; },
                "pen firmware text changed");
    CheckChange(baseline, [](State& state) { state.penHardware[0] = '1'; },
                "pen hardware text changed");
    CheckChange(baseline, [](State& state) { state.penSerial[0] = 'A'; },
                "pen serial text changed");
    CheckChange(baseline, [](State& state) { state.kbdModelName[0] = 'K'; },
                "keyboard model text changed");
    CheckChange(baseline, [](State& state) { state.kbdFirmware[0] = '1'; },
                "keyboard firmware text changed");
    CheckChange(baseline, [](State& state) {
        state.penSerial[PenStatus::kSerialCapacity - 1] = 'Z';
    }, "full fixed-size identity arrays participate in equality");

    auto timestampAndChange = baseline;
    timestampAndChange.updatedAtUnixMs += 1;
    timestampAndChange.charging = true;
    Check(!PenStatus::SameUiState(baseline, timestampAndChange),
          "ignoring time must not mask a simultaneous semantic change");
}

} // namespace

int main() {
    try {
        CheckTimestamps();
        CheckSemanticChanges();
        std::cout << "Pen status UI timestamp and semantic change checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
