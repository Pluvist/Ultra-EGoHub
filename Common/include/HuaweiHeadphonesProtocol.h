#pragma once
// Independent Windows implementation of the documented Huawei AAM wire format.
// Protocol references and model evidence: docs/headphones-protocol.md.
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace HuaweiHeadphones {
using Bytes = std::vector<uint8_t>;
enum class Model { Unknown, Buds3, Buds4, Buds4E, Buds5, Buds5i, Se4Anc,
    Buds6i, Buds6, Pro3, Pro4, Pro5, Buds7i, Buds7, Clip, Clip2, Arc, Eyewear, Eyewear2 };
struct Field { uint8_t tag; Bytes value; };
struct Frame { uint8_t service = 0, command = 0; std::vector<Field> fields;
    const Bytes* Find(uint8_t tag) const;
};
uint16_t Crc(std::span<const uint8_t> bytes);
Bytes Encode(uint8_t service, uint8_t command, std::span<const Field> fields);
Bytes Packet(uint8_t service, uint8_t command, std::initializer_list<Field> fields);
class Decoder {
public:
    std::vector<Frame> Feed(std::span<const uint8_t> bytes);
    void Reset() { m_bytes.clear(); }
private:
    Bytes m_bytes;
};
struct Identity { std::string resourceId, colorId; Model model = Model::Unknown; };
std::optional<Identity> ReadIdentity(const Frame& frame);
Model ModelFromName(std::wstring_view name);
Model ModelFromResource(std::string_view id);
std::wstring_view ModelName(Model model);
struct Battery { std::optional<int> level; bool charging = false; };
std::optional<std::array<Battery, 3>> ReadBattery(const Frame& frame);
Bytes BatteryQuery();
Bytes IdentityQuery();
// Completion is based on validated reply contents, never ACKs or a quiet timer.
// Keep this tracker local to one identity/battery transaction.
class ConnectionReplyProgress {
public:
    bool Observe(std::span<const Frame> frames) {
        for (const auto& frame : frames) {
            m_identity = m_identity || ReadIdentity(frame).has_value();
            m_battery = m_battery || ReadBattery(frame).has_value();
        }
        return m_identity && m_battery;
    }
private:
    bool m_identity = false, m_battery = false;
};
struct Choice { int value; std::wstring label; Bytes write; };
struct Control {
    std::string id;
    std::wstring label, group;
    std::vector<Choice> choices;
    Bytes query;
    uint8_t service = 0x2b, command = 0, field = 1;
    int index = 0; // -1 = final byte in a variable-size AAM value
    std::vector<std::pair<uint8_t, Bytes>> selectors;
    std::optional<uint8_t> mirrorField;
    std::optional<int> requiredNoiseMode;
    std::optional<int> Parse(const Frame& frame) const;
};
std::vector<Control> ControlProfile(Model model);
std::optional<Bytes> WriteControl(Model model, std::string_view id, int value);
bool SupportsCustomEqualizer(Model model);
std::optional<Bytes> CustomEqualizer(Model model, int slot,
    std::string_view name, std::span<const int> gains);
std::optional<std::array<int,10>> ReadEqualizer(const Frame& frame);
struct PairedPeer { std::array<uint8_t,6> address{}; std::string name; bool connected = false; };
std::optional<PairedPeer> ReadPairedPeer(const Frame& frame);
Bytes PairedPeersQuery(Model model);
std::optional<Bytes> ForgetPeer(Model model, const std::array<uint8_t,6>& address);
}
