#include "VhfReporter.h"
#include "PenHoverPolicy.h"
#include "VhfStylusState.h"
#include "TouchSolver/TsaCore/TouchTrace.h"
#include "VhfReporterStylusPacketHelper.h"
#include "VhfReporterTouchPacketHelper.h"
#include "Logger.h"

#include <Windows.h>
#include <SetupAPI.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <vector>

// ── VHF HID Injector GUID ──
const GUID VhfReporter::kVhfGuid =
    {0x59819b74, 0xf102, 0x469a,
     {0x90, 0x09, 0x3c, 0xaf, 0x35, 0xfd, 0x46, 0x86}};

// ── Helpers ──

namespace {

class DeviceInfoList {
public:
    explicit DeviceInfoList(const GUID& guid)
        : m_handle(SetupDiGetClassDevsW(
              &guid, nullptr, nullptr,
              DIGCF_PRESENT | DIGCF_DEVICEINTERFACE)) {}

    ~DeviceInfoList() {
        if (m_handle != INVALID_HANDLE_VALUE) {
            SetupDiDestroyDeviceInfoList(m_handle);
        }
    }

    DeviceInfoList(const DeviceInfoList&) = delete;
    DeviceInfoList& operator=(const DeviceInfoList&) = delete;

    [[nodiscard]] bool IsValid() const {
        return m_handle != INVALID_HANDLE_VALUE;
    }

    [[nodiscard]] HDEVINFO Get() const {
        return m_handle;
    }

private:
    HDEVINFO m_handle = INVALID_HANDLE_VALUE;
};

[[nodiscard]] bool HasTouchReports(
        const VhfTouchPacket::TouchPackets& packets) noexcept {
    return packets[0].valid || packets[1].valid;
}

[[nodiscard]] std::array<uint8_t, 17> MakeStylusBytes(
        const Solvers::StylusPacket& packet,
        uint8_t eraserState) {
    auto bytes = packet.bytes;
    VhfStylusState::ApplyTool(bytes, eraserState == 1u);
    return bytes;
}

} // namespace

// ── Lifecycle ──

VhfReporter::VhfReporter() = default;
VhfReporter::~VhfReporter() { Close(); }

void VhfReporter::SetStylusPacketSensorRows(int rows) {
    std::lock_guard<std::mutex> lk(m_mu);
    m_stylusSensorRows = std::max(rows, 1);
}

void VhfReporter::SetStylusPacketSensorCols(int cols) {
    std::lock_guard<std::mutex> lk(m_mu);
    m_stylusSensorCols = std::max(cols, 1);
}

void VhfReporter::SetStylusPacketEmitWhenInvalid(bool v) {
    std::lock_guard<std::mutex> lk(m_mu);
    m_emitStylusPacketWhenInvalid = v;
}

void VhfReporter::SetEnabled(bool v) {
    std::lock_guard<std::mutex> lk(m_mu);
    if (m_closed) { m_enabled.store(false); return; }
    const bool wasEnabled = m_enabled.exchange(v);
    if (wasEnabled && !v) {
        WriteTouchAllUpLocked();
        if (m_hadStylusActiveLastFrame.load()) WriteStylusNeutralLocked();
    }
}

void VhfReporter::FlushTouchAllUp() {
    std::lock_guard<std::mutex> lk(m_mu);
    if (!m_closed) WriteTouchAllUpLocked();
}

void VhfReporter::FlushAllUp() {
    std::lock_guard<std::mutex> lk(m_mu);
    if (m_closed) return;
    WriteTouchAllUpLocked();
    if (m_hadStylusActiveLastFrame.load()) WriteStylusNeutralLocked();
}

void VhfReporter::Close() {
    std::lock_guard<std::mutex> lk(m_mu);
    if (m_closed) return;
    if (m_handle != INVALID_HANDLE_VALUE) {
        WriteTouchAllUpLocked();
        if (m_hadStylusActiveLastFrame.load()) WriteStylusNeutralLocked();
    }
    m_closed = true;
    m_enabled.store(false);
    CloseDeviceLocked();
    m_nextOpenAttempt = {};
}

bool VhfReporter::IsDeviceOpen() const {
    std::lock_guard<std::mutex> lk(m_mu);
    return m_handle != INVALID_HANDLE_VALUE;
}

void VhfReporter::SetPenHoverEnabled(bool enabled) {
    std::lock_guard<std::mutex> lk(m_mu);
    if(m_penHoverEnabled==enabled)return;
    m_penHoverEnabled=enabled;
    // An in-progress stroke must survive toggling this setting.
    if(!enabled && !m_lastStylusTipDown &&
       m_hadStylusActiveLastFrame.load(std::memory_order_acquire) && !m_closed) {
        WriteStylusNeutralLocked();
    }
}

bool VhfReporter::EnsureReady() {
    std::lock_guard<std::mutex> lk(m_mu);
    return CanDispatchWriteLocked() && EnsureDeviceOpenLocked();
}

VhfReporter::StylusDispatchPacket VhfReporter::BuildStylusPacket(
        const Solvers::StylusFrameData& stylus) {
    VhfStylusPacket::Config config;
    {
        std::lock_guard<std::mutex> lk(m_mu);
        config.sensorRows = m_stylusSensorRows;
        config.sensorCols = m_stylusSensorCols;
        config.emitWhenInvalid = m_emitStylusPacketWhenInvalid;
    }
    config.barrelButton = m_barrelButtonState.exchange(
        false, std::memory_order_relaxed);

    StylusDispatchPacket built{};
    built.packet = VhfStylusPacket::Build(stylus, config);
    built.penState = VhfStylusPacket::ExtractPenState(built.packet);
    return built;
}



// ── 主入口 (legacy) ──

void VhfReporter::Dispatch(Solvers::HeatmapFrame& frame) {
    DispatchStylus(frame);
    DispatchTouch(frame);
}

// ── 独立手写笔写入 ──

void VhfReporter::DispatchStylus(Solvers::HeatmapFrame& frame,
                                 bool writeEnabled) {
    const auto stylusPacket = BuildStylusPacket(frame.stylus);
    if (!writeEnabled) {
        std::lock_guard<std::mutex> lk(m_mu);
        if (!m_closed && m_hadStylusActiveLastFrame.load()) WriteStylusNeutralLocked();
        return;
    }
    if (!m_enabled.load() || !stylusPacket.packet.valid) return;

    auto bytes = MakeStylusBytes(
        stylusPacket.packet,
        m_eraserState.load(std::memory_order_relaxed));

    std::lock_guard<std::mutex> lk(m_mu);
    if (!CanDispatchWriteLocked()) {
        return;
    }
    PenHoverPolicy::Apply(bytes,m_penHoverEnabled,frame.stylus.output.tipDown);
    WriteStylusPacketLocked(bytes.data(), stylusPacket.packet.length);
}

// ── 独立手指写入 ──

void VhfReporter::DispatchTouch(Solvers::HeatmapFrame& frame) {
    if (!m_enabled.load(std::memory_order_relaxed)) return;
    frame.touch.output.touchPackets = VhfTouchPacket::Build(
        frame.touch.output.contacts.span(), m_transpose.load(std::memory_order_relaxed));
    std::lock_guard<std::mutex> lk(m_mu);
    if (!CanDispatchWriteLocked()) return;
    if (HasTouchReports(frame.touch.output.touchPackets)) {
        WriteTouchPacketsLocked(frame.touch.output.touchPackets);
    } else {
        WriteTouchAllUpLocked();
    }
}

// ── 传输写入职责 (requires m_mu held) ──

void VhfReporter::WriteTouchPacketsLocked(
        const std::array<Solvers::TouchPacket, 2>& packets) {
    if (!CanDispatchWriteLocked() || !EnsureDeviceOpenLocked()) return;
    for (const auto& packet : packets) {
        if (packet.valid && !WritePacketLocked(packet.bytes.data(), packet.length, "touch")) {
            m_touchState.Observe(packets, false);
            return;
        }
    }
    m_touchState.Observe(packets, true);
    const size_t count=std::min<size_t>(packets[0].bytes[31],10);
    for(size_t i=0;i<count;++i){
        const auto& packet=packets[i/5];
        if(packet.valid){const auto* d=packet.bytes.data()+1+(i%5)*6;Solvers::Tsa::Core::TouchTrace::Hid(d[1],(d[0]&1)!=0);}
    }
}

void VhfReporter::WriteTouchAllUpLocked() {
    if (!m_touchState.HasActive() || !EnsureDeviceOpenLocked()) return;
    while (m_touchState.HasActive()) {
        const auto packets = m_touchState.ReleasePackets();
        for (const auto& packet : packets) {
            if (packet.valid && !WritePacketLocked(packet.bytes.data(), packet.length, "touch-up")) return;
        }
        m_touchState.Observe(packets, true);
    }
}

void VhfReporter::WriteStylusNeutralLocked() {
    if (!EnsureDeviceOpenLocked()) return;
    Solvers::StylusPacket neutral{};
    neutral.bytes[0] = neutral.reportId;
    VhfStylusState::KeepReleasePosition(neutral.bytes, m_lastStylusBytes);
    if (WritePacketLocked(neutral.bytes.data(), 13, "stylus-neutral")) {
        m_lastStylusBytes = neutral.bytes;
        m_hadStylusActiveLastFrame.store(false);
        m_lastStylusTipDown = false;
    }
}

void VhfReporter::WriteStylusPacketLocked(const uint8_t* data, size_t len) {
    if (!CanDispatchWriteLocked() || !EnsureDeviceOpenLocked()) return;
    std::array<uint8_t, 17> bytes{};
    std::copy_n(data, std::min(len, bytes.size()), bytes.begin());
    VhfStylusState::KeepReleasePosition(bytes, m_lastStylusBytes);
    if (WritePacketLocked(bytes.data(), len, "stylus")) {
        m_lastStylusBytes = bytes;
        m_hadStylusActiveLastFrame.store(VhfStylusState::Active(bytes));
        m_lastStylusTipDown = (bytes[1] & 0x09u) != 0; // Tip Switch or Eraser Switch
    }
}

// ── 设备 I/O ──

bool VhfReporter::CanDispatchWriteLocked() const {
    return m_enabled.load(std::memory_order_relaxed) && !m_closed;
}

bool VhfReporter::EnsureDeviceOpenLocked() {
    if (m_handle != INVALID_HANDLE_VALUE) {
        return true;
    }
    if (m_closed) {
        return false;
    }

    const auto now = std::chrono::steady_clock::now();
    if (now < m_nextOpenAttempt) {
        return false;
    }

    DeviceInfoList devInfo(kVhfGuid);
    if (!devInfo.IsValid()) {
        m_nextOpenAttempt = now + kReopenBackoff;
        return false;
    }

    for (DWORD idx = 0;; ++idx) {
        SP_DEVICE_INTERFACE_DATA ifData{};
        ifData.cbSize = sizeof(ifData);
        if (!SetupDiEnumDeviceInterfaces(
                devInfo.Get(), nullptr, &kVhfGuid, idx, &ifData)) {
            if (GetLastError() == ERROR_NO_MORE_ITEMS) {
                break;
            }
            continue;
        }

        DWORD reqSize = 0;
        SetupDiGetDeviceInterfaceDetailW(
            devInfo.Get(), &ifData, nullptr, 0, &reqSize, nullptr);
        if (reqSize < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) {
            continue;
        }

        alignas(SP_DEVICE_INTERFACE_DETAIL_DATA_W) std::array<uint8_t, 512> stackBuf{};
        std::vector<uint8_t> heapBuf;
        uint8_t* pBuf = stackBuf.data();
        if (reqSize > stackBuf.size()) {
            heapBuf.resize(reqSize, 0);
            pBuf = heapBuf.data();
        }

        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(pBuf);
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(
                devInfo.Get(), &ifData, detail, reqSize,
                nullptr, nullptr)) {
            continue;
        }

        HANDLE h = CreateFileW(
            detail->DevicePath,
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            m_handle = h;
            m_nextOpenAttempt = std::chrono::steady_clock::time_point{};
            LOG_INFO("VhfReporter", __func__, "VHF", "VHF device opened.");
            if (m_recoveringWrite) {
                m_recoveringWrite = false;
                WriteTouchAllUpLocked();
                if (m_handle != INVALID_HANDLE_VALUE) WriteStylusNeutralLocked();
                if (m_handle == INVALID_HANDLE_VALUE) return false;
            }
            return true;
        }
    }

    m_nextOpenAttempt = now + kReopenBackoff;
    return false;
}

void VhfReporter::CloseDeviceLocked() {
    if (m_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(m_handle);
        m_handle = INVALID_HANDLE_VALUE;
    }
}

void VhfReporter::ScheduleReopenLocked() {
    CloseDeviceLocked();
    m_recoveringWrite = true;
    m_nextOpenAttempt = std::chrono::steady_clock::now() +
                        kReopenBackoff;
}

bool VhfReporter::WritePacketLocked(const uint8_t* data, size_t len,
                                    const char* tag) {
    if (!data || len == 0 || m_handle == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD written = 0;
    const BOOL ok = WriteFile(m_handle, data,
                              static_cast<DWORD>(len),
                              &written, nullptr);
    if (ok && written == len) {
        return true;
    }

    const DWORD err = ok ? ERROR_WRITE_FAULT : GetLastError();
    LOG_WARN("VhfReporter", __func__, "VHF", "Write {} failed (len={}, written={}, err={}), scheduling reopen.", tag, (unsigned)len, (unsigned)written, (unsigned)err);
    ScheduleReopenLocked();
    return false;
}
