#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace winrt::EGoTouchSettings::implementation::AccessoryImages::Detail {

struct CropBounds {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
};

// BitmapBuffer rows may have padding, a nonzero start, or a negative stride.
struct BgraView {
    const uint8_t* data;
    size_t capacity;
    int32_t startIndex;
    int32_t stride;
    uint32_t width;
    uint32_t height;

    bool Valid() const noexcept {
        if (!data || !width || !height || startIndex < 0) return false;
        const int64_t rowBytes = static_cast<int64_t>(width) * 4;
        const int64_t step = stride;
        if ((step < 0 ? -step : step) < rowBytes) return false;
        const int64_t last = startIndex + static_cast<int64_t>(height - 1) * step;
        const int64_t firstByte = std::min<int64_t>(startIndex, last);
        const int64_t endByte = std::max<int64_t>(startIndex, last) + rowBytes;
        return firstByte >= 0 && static_cast<uint64_t>(endByte) <= capacity;
    }

    const uint8_t* Row(uint32_t y) const noexcept {
        return data + static_cast<ptrdiff_t>(
            startIndex + static_cast<int64_t>(y) * stride);
    }
};

inline CropBounds AlphaCropBounds(const BgraView& view) noexcept {
    uint32_t left = view.width;
    uint32_t top = view.height;
    uint32_t right = 0;
    uint32_t bottom = 0;
    bool found = false;
    for (uint32_t y = 0; y < view.height; ++y) {
        const uint8_t* row = view.Row(y);
        for (uint32_t x = 0; x < view.width; ++x) {
            if (row[static_cast<size_t>(x) * 4 + 3] <= 2) continue;
            found = true;
            left = std::min(left, x);
            top = std::min(top, y);
            right = std::max(right, x);
            bottom = std::max(bottom, y);
        }
    }
    if (!found) return {0, 0, view.width, view.height};

    const uint32_t contentWidth = right - left + 1;
    const uint32_t contentHeight = bottom - top + 1;
    const uint32_t padding = std::max(
        2u, static_cast<uint32_t>(std::lround(std::max(contentWidth, contentHeight) * 0.02)));
    const uint32_t x = left > padding ? left - padding : 0;
    const uint32_t y = top > padding ? top - padding : 0;
    const uint32_t cropRight = static_cast<uint32_t>(std::min<uint64_t>(
        view.width, static_cast<uint64_t>(right) + padding + 1));
    const uint32_t cropBottom = static_cast<uint32_t>(std::min<uint64_t>(
        view.height, static_cast<uint64_t>(bottom) + padding + 1));
    return {x, y, cropRight - x, cropBottom - y};
}

inline bool CopyCroppedRows(const BgraView& source, const CropBounds& bounds,
                            uint8_t* destination, size_t capacity,
                            int32_t startIndex, int32_t stride) noexcept {
    const BgraView output{destination, capacity, startIndex, stride,
                          bounds.width, bounds.height};
    if (!source.Valid() || !output.Valid() || bounds.x > source.width ||
        bounds.y > source.height || bounds.width > source.width - bounds.x ||
        bounds.height > source.height - bounds.y) return false;
    for (uint32_t y = 0; y < bounds.height; ++y) {
        std::memcpy(const_cast<uint8_t*>(output.Row(y)),
            source.Row(bounds.y + y) + static_cast<size_t>(bounds.x) * 4,
            static_cast<size_t>(bounds.width) * 4);
    }
    return true;
}

} // namespace winrt::EGoTouchSettings::implementation::AccessoryImages::Detail
