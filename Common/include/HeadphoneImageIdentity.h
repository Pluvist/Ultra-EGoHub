#pragma once
#include "HuaweiHeadphonesProtocol.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace HuaweiHeadphones::Images {
struct Identity {
    std::string modelId;
    std::string colorId;
    bool operator==(const Identity&) const = default;
};
enum class Part { Main, Left, Right };
inline constexpr std::string_view CdnOrigin = "https://smarthome-drcn.dbankcdn.cn";
inline constexpr std::string_view CdnPath = "/device/guide/AAM001/";
inline constexpr size_t MaxConfigBytes = 512 * 1024;
inline constexpr uint64_t MaxArchiveBytes = 64ull * 1024 * 1024;
inline constexpr uint64_t MaxPngBytes = 16ull * 1024 * 1024;

inline bool IsUpperHex(std::string_view value, size_t size) {
    return value.size() == size && std::all_of(value.begin(), value.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');
    });
}
inline bool ValidFields(const Identity& identity) {
    return IsUpperHex(identity.modelId, 6) && IsUpperHex(identity.colorId, 2) &&
        identity.modelId != "000000" && identity.modelId != "00000A";
}

// This catalog identifies a model's official resource directory. It deliberately
// supplies no color/default identity: legacy models need an explicit selection.
inline std::optional<std::string_view> CatalogModelId(Model model) {
    switch (model) {
    case Model::Buds3: return "000027";
    case Model::Buds4: return "000129";
    case Model::Buds4E: return "000135";
    case Model::Buds5: return "000141";
    case Model::Buds5i: return "000145";
    case Model::Se4Anc: return "000169";
    case Model::Buds6i: return "000153";
    case Model::Buds6: return "000155";
    case Model::Pro3: return "000149";
    case Model::Pro5: return "00016D";
    case Model::Buds7i: return "000163";
    case Model::Buds7: return "000174";
    case Model::Clip2: return "000167";
    case Model::Arc: return "00015D";
    case Model::Eyewear: return "000139";
    case Model::Eyewear2: return "00014F";
    default: return std::nullopt;
    }
}
inline bool ResourceMatchesModel(Model model, std::string_view resourceId) {
    if (model == Model::Unknown) return true;
    if (model == Model::Clip2 && resourceId == "000173") return true;
    if (auto expected = CatalogModelId(model)) return *expected == resourceId;
    // For a not-yet-catalogued model, do not substitute another known model's
    // assets. A genuinely new ID confirmed by DeviceInfo may still be fetched.
    for (int i = int(Model::Buds3); i <= int(Model::Eyewear2); ++i)
        if (auto id = CatalogModelId(Model(i)); id && *id == resourceId) return false;
    return resourceId != "000173";
}
inline std::optional<Identity> ConfirmedIdentity(
    Model model, std::string_view resourceId, std::string_view colorId) {
    Identity identity{std::string(resourceId), std::string(colorId)};
    if (!ValidFields(identity) || !ResourceMatchesModel(model, resourceId)) return std::nullopt;
    return identity;
}
inline std::optional<std::string> ConfigUrl(const Identity& identity) {
    if (!ValidFields(identity)) return std::nullopt;
    return std::string(CdnOrigin) + std::string(CdnPath) + identity.modelId + "/" + identity.modelId + ".json";
}
inline std::optional<std::string> ArchiveUrl(const Identity& identity) {
    if (!ValidFields(identity)) return std::nullopt;
    return std::string(CdnOrigin) + std::string(CdnPath) + identity.modelId + "/" +
        identity.modelId + "_" + identity.colorId + ".zip";
}
inline std::optional<std::string> CacheKey(const Identity& identity) {
    if (!ValidFields(identity)) return std::nullopt;
    return identity.modelId + "_" + identity.colorId;
}
inline std::string_view CacheImageName(Part part) {
    switch (part) {
    case Part::Left: return "left.png";
    case Part::Right: return "right.png";
    default: return "box.png";
    }
}

// Names are read from the central directory, not trusted filesystem paths.
// Callers must always write extracted bytes to their own fixed cache filenames.
inline bool SafeZipEntryName(std::string_view name) {
    if (name.empty() || name.size() > 1024 || name.front() == '/' || name.front() == '\\') return false;
    for (unsigned char c : name) if (c < 32 || c == 127 || c == ':') return false;
    size_t start = 0;
    for (size_t i = 0; i <= name.size(); ++i) {
        if (i != name.size() && name[i] != '/' && name[i] != '\\') continue;
        auto segment = name.substr(start, i - start);
        if (segment == "." || segment == "..") return false;
        if (segment.empty() && i != name.size()) return false;
        start = i + 1;
    }
    return true;
}
inline std::optional<size_t> EntryPriority(std::string_view name, Part part) {
    if (!SafeZipEntryName(name) || name.back() == '/' || name.back() == '\\') return std::nullopt;
    const size_t slash = name.find_last_of("/\\");
    auto file = name.substr(slash == std::string_view::npos ? 0 : slash + 1);
    std::string lower(file);
    for (char& c : lower) if (c >= 'A' && c <= 'Z') c = char(c + ('a' - 'A'));
    static constexpr std::array<std::string_view, 5> mainNames{
        "iv_device_logo.png", "device_icon.png", "mediacontroller_icon.png", "ota_device_icon.png", "ota_device.png"};
    static constexpr std::array<std::string_view, 4> leftNames{
        "find_left_icon.png", "find_left.png", "left_ear.png", "left_ear_icon.png"};
    static constexpr std::array<std::string_view, 4> rightNames{
        "find_right_icon.png", "find_right.png", "right_ear.png", "right_ear_icon.png"};
    const std::span<const std::string_view> names = part == Part::Main ?
        std::span<const std::string_view>(mainNames) : part == Part::Left ?
        std::span<const std::string_view>(leftNames) : std::span<const std::string_view>(rightNames);
    for (size_t i = 0; i < names.size(); ++i) if (lower == names[i]) return i;
    return std::nullopt;
}
inline std::optional<size_t> SelectEntry(std::span<const std::string> names, Part part) {
    std::optional<size_t> selected, priority;
    for (size_t i = 0; i < names.size(); ++i) {
        auto rank = EntryPriority(names[i], part);
        if (!rank) continue;
        if (!selected || *rank < *priority || (*rank == *priority &&
            (names[i].size() < names[*selected].size() ||
             (names[i].size() == names[*selected].size() && names[i] < names[*selected])))) {
            selected = i;
            priority = rank;
        }
    }
    return selected;
}
}
