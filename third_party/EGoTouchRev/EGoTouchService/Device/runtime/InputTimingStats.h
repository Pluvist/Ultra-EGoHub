#pragma once
#include <algorithm>
#include <cstdint>

// Worker-local aggregate timings. No allocation, per-frame logging or input data.
class InputTimingStats {
public:
    struct Window {
        uint64_t frames = 0, periods = 0, longPeriods = 0;
        int64_t periodSum = 0, maxPeriod = 0;
        int64_t readSum = 0, maxRead = 0;
        int64_t solveSum = 0, maxSolve = 0;
        int64_t sendSum = 0, maxSend = 0;
    };
    void Record(bool active, int64_t reportUs, int64_t readUs,
                int64_t solveUs, int64_t sendUs) noexcept {
        if (active && m_previousActive) {
            const auto period = std::max<int64_t>(0, reportUs - m_previousReport);
            ++m_window.periods;
            m_window.periodSum += period;
            m_window.maxPeriod = std::max(m_window.maxPeriod, period);
            if (period > 16000) ++m_window.longPeriods;
        }
        // The first active read can include seconds spent waiting in chip IDLE.
        // Exclude it so idle wake time isn't misreported as drag stutter.
        if (active && m_previousActive) {
            ++m_window.frames;
            m_window.readSum += readUs;
            m_window.solveSum += solveUs;
            m_window.sendSum += sendUs;
            m_window.maxRead = std::max(m_window.maxRead, readUs);
            m_window.maxSolve = std::max(m_window.maxSolve, solveUs);
            m_window.maxSend = std::max(m_window.maxSend, sendUs);
        }
        m_previousActive = active;
        m_previousReport = reportUs;
    }
    const Window& Read() const noexcept { return m_window; }
    void ResetWindow() noexcept { m_window = {}; }
    void Reset() noexcept { m_window = {}; m_previousActive = false; m_previousReport = 0; }
private:
    Window m_window{};
    bool m_previousActive = false;
    int64_t m_previousReport = 0;
};
