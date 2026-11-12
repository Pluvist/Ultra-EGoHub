#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace winrt::EGoTouchSettings::implementation::AccessoryImages::Detail {

using BitmapDigest = std::array<uint8_t, 32>;
inline constexpr size_t kBitmapCacheHeaderSize = 128;
inline constexpr uint64_t kBitmapCacheMaxBytes = 32u * 1024u * 1024u;
inline constexpr size_t kBitmapCacheMaxEntries = 8;
inline constexpr uint32_t kBitmapCacheMaxDimension = 4096;

struct CachedBitmapInfo {
    uint32_t width;
    uint32_t height;
    double dpiX;
    double dpiY;
    BitmapDigest source;
};

inline void StoreCacheInteger(std::span<uint8_t> bytes, size_t offset, uint64_t value,
                              size_t length) noexcept {
    for (size_t i = 0; i < length; ++i) bytes[offset + i] = static_cast<uint8_t>(value >> (i * 8));
}

inline uint64_t LoadCacheInteger(std::span<const uint8_t> bytes, size_t offset, size_t length) noexcept {
    uint64_t value = 0;
    for (size_t i = 0; i < length; ++i) value |= static_cast<uint64_t>(bytes[offset + i]) << (i * 8);
    return value;
}

inline bool ValidCacheInfo(const CachedBitmapInfo& info) noexcept {
    return info.width && info.height && info.width <= kBitmapCacheMaxDimension &&
        info.height <= kBitmapCacheMaxDimension && std::isfinite(info.dpiX) &&
        std::isfinite(info.dpiY) && info.dpiX > 0 && info.dpiY > 0 &&
        info.dpiX <= 65536 && info.dpiY <= 65536 &&
        static_cast<uint64_t>(info.width) * info.height * 4 + kBitmapCacheHeaderSize <= kBitmapCacheMaxBytes;
}

// Fixed little-endian format, independent of compiler packing. The final SHA-256
// covers bytes [0,96) AND the pixel payload, including dimensions, DPI and source.
inline std::array<uint8_t, kBitmapCacheHeaderSize> MakeCacheHeader(const CachedBitmapInfo& info) noexcept {
    std::array<uint8_t, kBitmapCacheHeaderSize> bytes{};
    constexpr std::array<uint8_t, 8> magic{'E', 'G', 'O', 'A', 'B', 'G', 'R', 'A'};
    std::copy(magic.begin(), magic.end(), bytes.begin());
    StoreCacheInteger(bytes, 8, 1, 4);  // File format version.
    StoreCacheInteger(bytes, 12, 1, 4); // Alpha>2, 2% padding crop algorithm v1.
    StoreCacheInteger(bytes, 16, kBitmapCacheHeaderSize, 4);
    StoreCacheInteger(bytes, 20, 1, 4); // Tightly packed BGRA8, premultiplied alpha.
    StoreCacheInteger(bytes, 24, info.width, 4);
    StoreCacheInteger(bytes, 28, info.height, 4);
    StoreCacheInteger(bytes, 32, static_cast<uint64_t>(info.width) * 4, 4);
    StoreCacheInteger(bytes, 40, static_cast<uint64_t>(info.width) * info.height * 4, 8);
    StoreCacheInteger(bytes, 48, std::bit_cast<uint64_t>(info.dpiX), 8);
    StoreCacheInteger(bytes, 56, std::bit_cast<uint64_t>(info.dpiY), 8);
    std::copy(info.source.begin(), info.source.end(), bytes.begin() + 64);
    return bytes;
}

inline std::optional<CachedBitmapInfo> ParseCacheHeader(
        std::span<const uint8_t> bytes, uint64_t fileSize, const BitmapDigest& expectedSource) noexcept {
    constexpr std::array<uint8_t, 8> magic{'E', 'G', 'O', 'A', 'B', 'G', 'R', 'A'};
    if (bytes.size() != kBitmapCacheHeaderSize || !std::equal(magic.begin(), magic.end(), bytes.begin()) ||
        LoadCacheInteger(bytes, 8, 4) != 1 || LoadCacheInteger(bytes, 12, 4) != 1 ||
        LoadCacheInteger(bytes, 16, 4) != kBitmapCacheHeaderSize || LoadCacheInteger(bytes, 20, 4) != 1 ||
        LoadCacheInteger(bytes, 36, 4) != 0 ||
        !std::equal(expectedSource.begin(), expectedSource.end(), bytes.begin() + 64)) return std::nullopt;
    CachedBitmapInfo info{static_cast<uint32_t>(LoadCacheInteger(bytes, 24, 4)),
        static_cast<uint32_t>(LoadCacheInteger(bytes, 28, 4)),
        std::bit_cast<double>(LoadCacheInteger(bytes, 48, 8)),
        std::bit_cast<double>(LoadCacheInteger(bytes, 56, 8)), expectedSource};
    if (!ValidCacheInfo(info)) return std::nullopt;
    const uint64_t stride = static_cast<uint64_t>(info.width) * 4;
    const uint64_t pixelBytes = stride * info.height;
    if (LoadCacheInteger(bytes, 32, 4) != stride || LoadCacheInteger(bytes, 40, 8) != pixelBytes ||
        fileSize != kBitmapCacheHeaderSize + pixelBytes) return std::nullopt;
    return info;
}

inline bool CacheChecksumMatches(std::span<const uint8_t> header, const BitmapDigest& computed) noexcept {
    return header.size() == kBitmapCacheHeaderSize &&
        std::equal(computed.begin(), computed.end(), header.begin() + 96);
}

} // namespace winrt::EGoTouchSettings::implementation::AccessoryImages::Detail
