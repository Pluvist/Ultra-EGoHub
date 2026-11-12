#pragma once

#include <chrono>
#include <string>

// SCM controller for the native EGoTouchRev driver.
namespace EGoTouch::Driver {

enum class StartResult {
    Started = 0,
    AlreadyRunning,
    HostNotFound,      ///< hostExePath 指向的文件不存在
    LaunchFailed,      ///< 注册或启动宿主服务失败
    ExitedImmediately, ///< 服务起来了但很快停止，通常是设备仍被占用或原厂目录定位失败
};

class HostController {
public:
    HostController() noexcept = default;
    ~HostController() noexcept;

    HostController(const HostController &) = delete;
    HostController &operator=(const HostController &) = delete;

    // Start on demand; --parent lets the driver recover Huawei after a Hub crash.
    [[nodiscard]] StartResult Start(const std::wstring &hostExePath,
                                    const std::wstring &extraArgs = {}) noexcept;

    // 请求宿主停止并等待它退出。超时后强制终止并返回 false——那种情况下设备可能停在
    // 中间状态，调用方应当在交还原厂服务前把这一点记进日志。
    // true 表示停止完成，允许交接；非零进程退出码保存在 ExitCode，不阻止恢复华为。
    [[nodiscard]] bool Stop(std::chrono::milliseconds timeout = std::chrono::seconds(15)) noexcept;

    [[nodiscard]] bool IsRunning() const noexcept;

    // 停止时的 SCM 错误码或非零进程退出码；超时为 ERROR_TIMEOUT，启动后为 -1。
    [[nodiscard]] int ExitCode() const noexcept;

private:
    // Retain the process identity after SCM drops its PID on SERVICE_STOPPED.
    mutable void *m_process = nullptr;
    int m_lastExitCode = -1;
};

} // namespace EGoTouch::Driver
