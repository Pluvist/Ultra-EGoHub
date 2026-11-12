#pragma once
#include "VhfReporterTouchPacketHelper.h"

// Last contacts potentially delivered to Windows, in encoded HID coordinates.
// A zero Contact Count is a hybrid continuation, not an explicit lift.
class VhfTouchState {
public:
    bool HasActive() const noexcept {
        return m_activeCount != 0;
    }

    void Observe(const VhfTouchPacket::TouchPackets& packets, bool delivered) noexcept {
        const size_t count = std::min<size_t>(packets[0].bytes[31], 10);
        for (size_t i = 0; i < count; ++i) {
            const auto& packet = packets[i / 5];
            if (!packet.valid) continue;
            const uint8_t* data = packet.bytes.data() + 1 + (i % 5) * 6;
            auto& last = m_contacts[data[1]];
            // Remember possible downs even if the second hybrid packet fails.
            // Clear a pending lift only after the entire frame was delivered.
            if (data[0] & 1u) {
                // A failed move must not replace the last confirmed position.
                if (!(last[0] & 1u)) ++m_activeCount;
                if (delivered || !(last[0] & 1u)) std::copy_n(data, 6, last.begin());
            } else if (delivered) {
                if (last[0] & 1u) --m_activeCount;
                last = {};
            }
        }
    }

    VhfTouchPacket::TouchPackets ReleasePackets() const noexcept {
        VhfTouchPacket::TouchPackets packets{};
        for (auto& p : packets) p.bytes[0] = p.reportId;
        size_t count = 0;
        for (const auto& last : m_contacts) {
            if (!(last[0] & 1u)) continue;
            auto& p = packets[count / 5];
            auto* data = p.bytes.data() + 1 + (count % 5) * 6;
            std::copy(last.begin(), last.end(), data);
            data[0] &= ~1u; // tip up; same ID and last transmitted coordinates
            if (++count == 10) break;
        }
        packets[0].bytes[31] = static_cast<uint8_t>(count);
        packets[0].valid = count > 0;
        packets[1].valid = count > 5;
        return packets;
    }

private:
    size_t m_activeCount = 0;
    std::array<std::array<uint8_t, 6>, 256> m_contacts{};
};
