#include "pch.h"
#include "HeadphoneImageLoader.h"
#include "HeadphoneImageIdentity.h"

#include <filesystem>
#include <fstream>
#include <span>

namespace winrt::EGoTouchSettings::implementation::AccessoryImages {
namespace {
namespace fs = std::filesystem;
namespace Policy = HuaweiHeadphones::Images;
constexpr size_t kMaxFiles = 4096;
constexpr uint32_t kMaxDimension = 4096;

uint32_t BigEndian(std::span<const uint8_t> data, size_t offset) {
    return (uint32_t(data[offset]) << 24) | (uint32_t(data[offset + 1]) << 16) |
        (uint32_t(data[offset + 2]) << 8) | uint32_t(data[offset + 3]);
}

uint32_t PngCrc(std::span<const uint8_t> data) {
    uint32_t crc = 0xffffffffu;
    for (auto byte : data) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
    }
    return ~crc;
}

bool SafePng(std::span<const uint8_t> bytes) {
    static constexpr std::array<uint8_t, 8> signature{137, 80, 78, 71, 13, 10, 26, 10};
    if (bytes.size() < 57 || bytes.size() > Policy::MaxPngBytes ||
        !std::equal(signature.begin(), signature.end(), bytes.begin())) return false;

    bool header = false;
    size_t idatBytes = 0;
    size_t pos = 8;
    size_t chunks = 0;
    while (pos < bytes.size()) {
        if (++chunks > 4096 || bytes.size() - pos < 12) return false;
        const size_t size = BigEndian(bytes, pos);
        if (size > bytes.size() - pos - 12) return false;
        const auto type = BigEndian(bytes, pos + 4);
        if (PngCrc(bytes.subspan(pos + 4, size + 4)) != BigEndian(bytes, pos + 8 + size)) return false;
        if (!header) {
            if (type != 0x49484452u || size != 13) return false; // IHDR
            const uint32_t width = BigEndian(bytes, pos + 8);
            const uint32_t height = BigEndian(bytes, pos + 12);
            if (!width || !height || width > kMaxDimension || height > kMaxDimension) return false;
            const uint8_t depth = bytes[pos + 16], color = bytes[pos + 17];
            const bool legalDepth = color == 0 ? (depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16) :
                color == 3 ? (depth == 1 || depth == 2 || depth == 4 || depth == 8) :
                (color == 2 || color == 4 || color == 6) && (depth == 8 || depth == 16);
            if (!legalDepth || bytes[pos + 18] != 0 || bytes[pos + 19] != 0 || bytes[pos + 20] > 1) return false;
            header = true;
        } else if (type == 0x49484452u) {
            return false;
        }
        if (type == 0x49444154u) idatBytes += size; // IDAT
        if (type == 0x49454e44u) // IEND must terminate the complete file.
            return size == 0 && idatBytes != 0 && pos + 12 == bytes.size();
        pos += size + 12;
    }
    return false;
}

fs::path AssetRoot() {
    std::vector<wchar_t> module(32768);
    const DWORD length = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
    if (!length || length >= module.size()) return {};
    return fs::path(std::wstring(module.data(), length)).parent_path() / L"Assets" / L"Headphones";
}

Asset ReadPng(const fs::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return {};
    const std::streamoff length = file.tellg();
    if (length <= 0 || uint64_t(length) > Policy::MaxPngBytes) return {};
    std::vector<uint8_t> bytes(static_cast<size_t>(length));
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), length) || !SafePng(bytes)) return {};
    return Asset{std::move(bytes)};
}
}

Asset ResolveHeadphone(const HuaweiHeadphones::Snapshot& state) {
    try {
        const auto identity = Policy::ConfirmedIdentity(state.model, state.resourceId, state.colorId);
        if (!identity) return {};
        const auto key = Policy::CacheKey(*identity);
        const fs::path root = AssetRoot();
        if (!key || root.empty()) return {};
        const fs::path directory = root / fs::path(*key);
        std::error_code error;
        if (!fs::is_directory(directory, error) || error || fs::is_symlink(directory, error)) return {};
        std::vector<std::string> names;
        std::vector<fs::path> paths;
        size_t count = 0;
        for (fs::recursive_directory_iterator it(directory, fs::directory_options::skip_permission_denied, error), end;
             !error && it != end; it.increment(error)) {
            if (++count > kMaxFiles) return {};
            if (it->is_symlink(error) || error || !it->is_regular_file(error) || error) continue;
            const auto relative = it->path().lexically_relative(directory).generic_u8string();
            std::string name(relative.begin(), relative.end());
            if (!Policy::SafeZipEntryName(name)) continue;
            names.push_back(std::move(name));
            paths.push_back(it->path());
        }
        // Do not substitute another identity or an arbitrary PNG. Try the known
        // primary-image names, then verified left/right product image names.
        for (const auto part : {Policy::Part::Main, Policy::Part::Left, Policy::Part::Right}) {
            while (auto selected = Policy::SelectEntry(names, part)) {
                if (auto asset = ReadPng(paths[*selected])) return asset;
                names[*selected].clear();
            }
        }
    } catch (...) {
        // Optional product art must never block the accessory controls/toast.
    }
    return {};
}
}
