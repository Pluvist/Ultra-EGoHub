#include "../../Tools/EGoTouchSettings/AccessoryBitmapCache.h"

#include <iostream>
#include <limits>
#include <stdexcept>

namespace Cache = winrt::EGoTouchSettings::implementation::AccessoryImages::Detail;

static void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    try {
        Cache::BitmapDigest source{};
        for (size_t i = 0; i < source.size(); ++i) source[i] = static_cast<uint8_t>(i);
        const Cache::CachedBitmapInfo info{123, 45, 96.123456789, 72.5, source};
        const auto original = Cache::MakeCacheHeader(info);
        const uint64_t fileSize = Cache::kBitmapCacheHeaderSize + uint64_t{123} * 45 * 4;
        const auto parsed = Cache::ParseCacheHeader(original, fileSize, source);
        Check(parsed && parsed->width == info.width && parsed->height == info.height &&
            parsed->dpiX == info.dpiX && parsed->dpiY == info.dpiY && parsed->source == source,
            "Dimensions, full-precision DPI and source SHA round trip");
        Check(original[24] == 123 && original[28] == 45 && original[32] == 236 && original[33] == 1,
            "Canonical little-endian width, height and tightly packed stride");
        Check(Cache::kBitmapCacheMaxEntries == 8 && Cache::kBitmapCacheMaxBytes == 32u * 1024u * 1024u,
            "Cache quota remains eight files and 32 MiB including headers");
        Check(!Cache::ParseCacheHeader({}, fileSize, source) &&
            !Cache::ParseCacheHeader(std::span{original}.first(127), fileSize, source),
            "Truncated headers are rejected before field reads");
        std::array<uint8_t, 129> extended{};
        std::copy(original.begin(), original.end(), extended.begin());
        Check(!Cache::ParseCacheHeader(extended, fileSize, source), "Unknown header extension rejected");
        for (uint64_t length : {uint64_t{0}, uint64_t{127}, fileSize - 1, fileSize + 1,
                                std::numeric_limits<uint64_t>::max()}) {
            Check(!Cache::ParseCacheHeader(original, length, source), "Truncated or trailing payload rejected");
        }
        for (size_t offset : {0u, 7u, 8u, 12u, 16u, 20u, 36u}) {
            auto damaged = original;
            damaged[offset] ^= 1;
            Check(!Cache::ParseCacheHeader(damaged, fileSize, source),
                "Magic, format/crop version, header length, pixel layout and reserved data enforced");
        }
        for (size_t offset : {32u, 40u}) {
            auto damaged = original;
            Cache::StoreCacheInteger(damaged, offset, std::numeric_limits<uint32_t>::max(), 4);
            Check(!Cache::ParseCacheHeader(damaged, fileSize, source), "Forged stride or byte count rejected");
        }
        for (size_t i : {size_t{0}, size_t{31}}) {
            auto other = source;
            other[i] ^= 1;
            Check(!Cache::ParseCacheHeader(original, fileSize, other), "Different original image cannot reuse this cache");
        }

        auto checkInfo = [&](Cache::CachedBitmapInfo candidate, bool expected) {
            const uint64_t length = Cache::kBitmapCacheHeaderSize +
                static_cast<uint64_t>(candidate.width) * candidate.height * 4;
            Check(Cache::ValidCacheInfo(candidate) == expected, "Dimension/DPI allocation guard");
            Check(Cache::ParseCacheHeader(Cache::MakeCacheHeader(candidate), length, source).has_value() == expected,
                "Serialized allocation guard matches metadata guard");
        };
        checkInfo({1, 1, 96, 96, source}, true);
        checkInfo({4096, 1, 96, 96, source}, true);
        checkInfo({1, 4096, 96, 96, source}, true);
        checkInfo({0, 1, 96, 96, source}, false);
        checkInfo({1, 0, 96, 96, source}, false);
        checkInfo({4097, 1, 96, 96, source}, false);
        checkInfo({1, 4097, 96, 96, source}, false);
        checkInfo({std::numeric_limits<uint32_t>::max(), 1, 96, 96, source}, false);
        // 2336*3591*4 + 128 is exactly 32 MiB, so the header is included in the limit.
        checkInfo({2336, 3591, 96, 96, source}, true);
        checkInfo({2336, 3592, 96, 96, source}, false);
        checkInfo({4096, 2048, 96, 96, source}, false);
        for (double dpi : {0.0, -1.0, std::numeric_limits<double>::infinity(),
                           std::numeric_limits<double>::quiet_NaN(), 65537.0}) {
            checkInfo({1, 1, dpi, 96, source}, false);
            checkInfo({1, 1, 96, dpi, source}, false);
        }
        checkInfo({1, 1, 65536, 65536, source}, true);

        Cache::BitmapDigest computed{};
        computed[0] = 0xA5; computed[31] = 0x5A;
        auto signedHeader = original;
        std::copy(computed.begin(), computed.end(), signedHeader.begin() + 96);
        Check(Cache::CacheChecksumMatches(signedHeader, computed), "Complete SHA-256 comparison accepted");
        for (size_t i = 0; i < computed.size(); ++i) {
            auto mismatch = computed;
            mismatch[i] ^= 1;
            Check(!Cache::CacheChecksumMatches(signedHeader, mismatch), "Every SHA-256 byte is checked");
        }
        Check(!Cache::CacheChecksumMatches(std::span{signedHeader}.first(127), computed),
            "Truncated checksum field rejected");
        std::cout << "Accessory bitmap cache format, versions, dimensions, DPI, lengths, source and checksum guards passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
