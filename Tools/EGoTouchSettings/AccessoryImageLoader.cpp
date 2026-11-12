#include "pch.h"

#include "AccessoryImageLoader.h"
#include "AccessoryBitmapCache.h"
#include "AccessoryBitmapCrop.h"
#include "ManagedResource.h"

#include <MemoryBuffer.h>
#include <bcrypt.h>
#include <chrono>
#include <fstream>
#include <list>
#include <limits>
#include <mutex>
#include <optional>
#include <unordered_map>

#pragma comment(lib, "bcrypt.lib")

using namespace winrt;
using namespace winrt::Windows::Graphics::Imaging;
using namespace winrt::Windows::Storage::Streams;
using namespace Microsoft::UI::Xaml::Media;
using namespace Microsoft::UI::Xaml::Media::Imaging;

namespace winrt::EGoTouchSettings::implementation::AccessoryImages {

namespace {

std::wstring PCManagerRoot() {
    wchar_t root[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_PROGRAM_FILES, nullptr, 0, root))) return {};
    return std::wstring{root} + L"\\Huawei\\PCManager\\";
}

std::vector<uint8_t> ReadFileBytes(const std::wstring& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return {};
    const std::streamoff length = file.tellg();
    if (length <= 0) return {};
    std::vector<uint8_t> bytes(static_cast<size_t>(length));
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), length)) return {};
    return bytes;
}

Asset FileAsset(const std::wstring& path) {
    if (path.empty() || GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return {};
    auto bytes = ReadFileBytes(path);
    if (bytes.empty()) return {};
    return Asset{std::move(bytes)};
}

std::wstring PenPluginPath(uint32_t modelId) {
    const std::wstring root = PCManagerRoot();
    if (root.empty()) return {};
    const wchar_t* dll = nullptr;
    switch (modelId) {
    case 0x00011Au: dll = L"CD52PenApp.dll"; break;
    case 0x00011Bu: dll = L"CD54PenApp.dll"; break;
    case 0x01011Bu: dll = L"CD54RPenApp.dll"; break;
    case 0x443002u: dll = L"CD54SPenApp.dll"; break;
    default: return {};
    }
    return root +
        L"components\\accessories_center\\accessories_app\\AccessoryApp\\Lib\\Plugins\\" +
        dll;
}

std::wstring PenConnectResourceName(uint32_t modelId) {
    wchar_t name[64]{};
    std::swprintf(name, std::size(name),
                  L"resources/pic_%08x_connect@1.5x.png", modelId << 8);
    return name;
}

Asset EmbeddedAsset(const std::wstring& plugin, const std::wstring& resource) {
    if (plugin.empty() || resource.empty()) return {};
    std::vector<uint8_t> bytes;
    if (!ManagedRes::ReadEmbeddedResource(plugin, resource, bytes) || bytes.empty()) return {};
    return Asset{std::move(bytes)};
}

Asset ResolveGenericPen() {
    const std::wstring root = PCManagerRoot();
    if (root.empty()) return {};
    const std::wstring pluginRoot = root +
        L"components\\accessories_center\\accessories_app\\AccessoryApp\\Lib\\Plugins\\";
    constexpr const wchar_t* plugins[] = {
        L"CD54PenApp.dll", L"CD54RPenApp.dll", L"CD52PenApp.dll", L"CD54SPenApp.dll",
    };
    for (const wchar_t* dll : plugins) {
        if (auto asset = EmbeddedAsset(
                pluginRoot + dll, L"resources/pic_vector_connect@1.5x.png")) {
            return asset;
        }
    }
    return {};
}

} // namespace

Asset ResolvePen(uint32_t modelId) {
    // Settings 进程启动时设备页先解析；弹窗随后拿同一份缓存，因此即使 PC Manager 正在更新
    // 资源，两处也不会一个取到卡片图、另一个取到插件回退图。
    static std::mutex cacheMutex;
    static std::unordered_map<uint32_t, Asset> cache;
    std::lock_guard<std::mutex> lock(cacheMutex);
    if (const auto it = cache.find(modelId); it != cache.end()) return it->second;

    Asset resolved;
    const std::wstring root = PCManagerRoot();
    if (!root.empty() && modelId != 0) {
        const std::wstring card = root +
            L"components\\accessories_center\\res\\drawable\\cards\\" +
            std::to_wstring(modelId) + L"_00.png";
        resolved = FileAsset(card);
    }

    const std::wstring plugin = resolved ? std::wstring{} : PenPluginPath(modelId);
    if (!resolved && !plugin.empty()) {
        resolved = EmbeddedAsset(plugin, PenConnectResourceName(modelId));
        // 当前安装版 CD54R 插件没有自己的连接图，原厂也回退到 CD54 银色笔图。
        if (!resolved && modelId == 0x01011Bu) {
            resolved = EmbeddedAsset(plugin, PenConnectResourceName(0x00011Bu));
        }
        if (!resolved) {
            resolved = EmbeddedAsset(plugin, L"resources/pic_vector_connect@1.5x.png");
        }
    }
    if (!resolved) resolved = ResolveGenericPen();
    if (resolved) cache.emplace(modelId, resolved);
    return resolved;
}

Asset ResolveKeyboard() {
    static std::mutex cacheMutex;
    static std::optional<Asset> cache;
    std::lock_guard<std::mutex> lock(cacheMutex);
    if (cache) return *cache;

    const std::wstring root = PCManagerRoot();
    if (root.empty()) return {};
    const std::wstring cards =
        root + L"components\\accessories_center\\res\\drawable\\cards\\";
    Asset resolved = FileAsset(cards + L"RX0H_00.png");
    if (!resolved) resolved = FileAsset(cards + L"RX0I_00.png");
    if (resolved) cache = resolved;
    return resolved;
}

Asset ResolveMachine() {
    static std::mutex cacheMutex;
    static std::optional<Asset> cache;
    std::lock_guard<std::mutex> lock(cacheMutex);
    if (cache) return *cache;

    const std::wstring root = PCManagerRoot();
    if (root.empty()) return {};

    const std::wstring plugin = root +
        L"components\\accessories_center\\accessories_app\\AccessoryApp\\Lib\\Plugins\\"
        L"GaokunKeyboardApp.dll";

    // 分离态与吸附态各有一张 648x648，画的都是平板加键盘。取吸附态：那是这台机器平时的
    // 样子，两个部件贴合成一体，也更接近产品照。
    Asset resolved = EmbeddedAsset(plugin, L"resources/snapping.png");
    if (!resolved) resolved = EmbeddedAsset(plugin, L"resources/detach.png");
    if (resolved) cache = resolved;
    return resolved;
}

namespace {

struct PreparedBitmap {
    SoftwareBitmap bitmap{nullptr};
    Windows::Foundation::Size size{};
    std::optional<Detail::BitmapDigest> diskSource;
};

struct DecodedEntry {
    std::vector<uint8_t> encoded;
    PreparedBitmap prepared;
    size_t bytes;
    std::chrono::steady_clock::time_point lastDiskTouch;
};

void TouchDiskBitmap(const Detail::BitmapDigest& source) noexcept;

// Only images actually displayed enter this process-local LRU. Compare the full
// original bytes so equal-size files or hash collisions cannot select wrong art.
struct DecodedCache {
    std::mutex mutex;
    std::list<DecodedEntry> entries;
    size_t bytes = 0;
};

DecodedCache& BitmapCache() {
    static DecodedCache cache;
    return cache;
}

std::optional<PreparedBitmap> FindPreparedBitmap(const std::vector<uint8_t>& encoded) {
    auto& cache = BitmapCache();
    std::optional<PreparedBitmap> prepared;
    std::optional<Detail::BitmapDigest> touch;
    {
        std::lock_guard lock(cache.mutex);
        for (auto entry = cache.entries.begin(); entry != cache.entries.end(); ++entry) {
            if (entry->encoded != encoded) continue;
            const auto now = std::chrono::steady_clock::now();
            if (now - entry->lastDiskTouch >= std::chrono::minutes(1)) {
                touch = entry->prepared.diskSource;
                entry->lastDiskTouch = now;
            }
            cache.entries.splice(cache.entries.begin(), cache.entries, entry);
            prepared = cache.entries.front().prepared;
            break;
        }
    }
    if (touch) TouchDiskBitmap(*touch); // All callers are already on the background apartment.
    return prepared;
}

PreparedBitmap RememberPreparedBitmap(std::vector<uint8_t> encoded, PreparedBitmap prepared) {
    constexpr size_t kMaxEntries = 8;
    constexpr size_t kMaxBytes = 32u * 1024u * 1024u;
    const uint64_t bytes = static_cast<uint64_t>(encoded.size()) +
        static_cast<uint64_t>(prepared.bitmap.PixelWidth()) * prepared.bitmap.PixelHeight() * 4;
    if (bytes > kMaxBytes) return prepared;

    auto& cache = BitmapCache();
    std::lock_guard lock(cache.mutex);
    // Concurrent first requests may finish together. Retain only one result.
    for (auto entry = cache.entries.begin(); entry != cache.entries.end(); ++entry) {
        if (entry->encoded != encoded) continue;
        cache.entries.splice(cache.entries.begin(), cache.entries, entry);
        return cache.entries.front().prepared;
    }
    while (!cache.entries.empty() &&
           (cache.entries.size() >= kMaxEntries || cache.bytes + bytes > kMaxBytes)) {
        cache.bytes -= cache.entries.back().bytes;
        cache.entries.pop_back();
    }
    cache.entries.push_front({std::move(encoded), prepared, static_cast<size_t>(bytes),
                              std::chrono::steady_clock::now()});
    cache.bytes += static_cast<size_t>(bytes);
    return prepared;
}

PreparedBitmap CropBitmap(const SoftwareBitmap& decoded) {
    const auto width = static_cast<uint32_t>(decoded.PixelWidth());
    const auto height = static_cast<uint32_t>(decoded.PixelHeight());
    SoftwareBitmap cropped{nullptr};
    Detail::CropBounds bounds{};
    {
        const BitmapBuffer buffer = decoded.LockBuffer(BitmapBufferAccessMode::Read);
        const auto reference = buffer.CreateReference();
        uint8_t* pixels = nullptr;
        uint32_t capacity = 0;
        check_hresult(reference.as<::Windows::Foundation::IMemoryBufferByteAccess>()->GetBuffer(
            &pixels, &capacity));
        const auto plane = buffer.GetPlaneDescription(0);
        const Detail::BgraView source{pixels, capacity, plane.StartIndex, plane.Stride, width, height};
        if (!source.Valid()) throw hresult_error(E_BOUNDS);
        bounds = Detail::AlphaCropBounds(source);
        if (bounds.x || bounds.y || bounds.width != width || bounds.height != height) {
            cropped = SoftwareBitmap(BitmapPixelFormat::Bgra8,
                                      static_cast<int32_t>(bounds.width),
                                      static_cast<int32_t>(bounds.height),
                                      BitmapAlphaMode::Premultiplied);
            cropped.DpiX(decoded.DpiX());
            cropped.DpiY(decoded.DpiY());
            const BitmapBuffer output = cropped.LockBuffer(BitmapBufferAccessMode::Write);
            const auto outputReference = output.CreateReference();
            uint8_t* outputPixels = nullptr;
            uint32_t outputCapacity = 0;
            check_hresult(outputReference.as<::Windows::Foundation::IMemoryBufferByteAccess>()->GetBuffer(
                &outputPixels, &outputCapacity));
            const auto outputPlane = output.GetPlaneDescription(0);
            if (!Detail::CopyCroppedRows(source, bounds, outputPixels, outputCapacity,
                                         outputPlane.StartIndex, outputPlane.Stride)) {
                throw hresult_error(E_BOUNDS);
            }
        }
    }
    if (!cropped) cropped = decoded;
    // All cached consumers are readers; keep that contract enforced by WinRT.
    return {cropped.GetReadOnlyView(), Windows::Foundation::Size{
        static_cast<float>(bounds.width), static_cast<float>(bounds.height)}};
}

std::optional<Detail::BitmapDigest> BitmapSha256(
        std::span<const uint8_t> first, std::span<const uint8_t> second = {}) noexcept {
    struct HashHandle {
        BCRYPT_HASH_HANDLE value = nullptr;
        ~HashHandle() { if (value) BCryptDestroyHash(value); }
    } hash;
    if (BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE, &hash.value, nullptr, 0, nullptr, 0, 0) < 0)
        return std::nullopt;
    for (auto part : {first, second}) {
        while (!part.empty()) {
            const ULONG length = static_cast<ULONG>(std::min<size_t>(
                part.size(), std::numeric_limits<ULONG>::max()));
            if (BCryptHashData(hash.value, const_cast<PUCHAR>(part.data()), length, 0) < 0)
                return std::nullopt;
            part = part.subspan(length);
        }
    }
    Detail::BitmapDigest digest{};
    if (BCryptFinishHash(hash.value, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0)
        return std::nullopt;
    return digest;
}

std::wstring DigestName(std::span<const uint8_t> digest) {
    constexpr wchar_t hex[] = L"0123456789abcdef";
    std::wstring result;
    result.reserve(digest.size() * 2);
    for (uint8_t byte : digest) {
        result.push_back(hex[byte >> 4]);
        result.push_back(hex[byte & 15]);
    }
    return result;
}

bool CacheFileName(std::wstring_view name) noexcept {
    return name.size() == 69 && name.substr(64) == L".bgra" &&
        std::all_of(name.begin(), name.begin() + 64,
            [](wchar_t c) { return (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f'); });
}

bool CacheTempName(std::wstring_view name) noexcept {
    return name.size() == 106 && CacheFileName(name.substr(0, 69)) && name[69] == L'.' &&
        name.substr(102) == L".tmp" && std::all_of(name.begin() + 70, name.begin() + 102,
            [](wchar_t c) { return (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f'); });
}

std::wstring DiskCacheDirectory() {
    static const std::wstring localRoot = [] {
        PWSTR path = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path))) return std::wstring{};
        std::wstring result{path};
        CoTaskMemFree(path);
        return result;
    }();
    if (localRoot.empty()) return {};
    std::wstring path = localRoot;
    for (const wchar_t* part : {L"OpenEGoHub", L"AccessoryImages", L"v1"}) {
        path += L"\\";
        path += part;
        if (!CreateDirectoryW(path.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return {};
        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return {};
    }
    return path;
}

struct CacheFileHandle {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~CacheFileHandle() { Close(); }
    explicit operator bool() const noexcept { return value != INVALID_HANDLE_VALUE; }
    void Close() noexcept {
        if (value != INVALID_HANDLE_VALUE) CloseHandle(value);
        value = INVALID_HANDLE_VALUE;
    }
};

std::mutex& DiskCacheMutex() {
    static std::mutex mutex;
    return mutex;
}

bool ReadCacheBytes(HANDLE file, std::span<uint8_t> bytes) noexcept {
    while (!bytes.empty()) {
        DWORD read = 0;
        const DWORD length = static_cast<DWORD>(std::min<size_t>(bytes.size(), MAXDWORD));
        if (!ReadFile(file, bytes.data(), length, &read, nullptr) || read == 0) return false;
        bytes = bytes.subspan(read);
    }
    return true;
}

bool WriteCacheBytes(HANDLE file, std::span<const uint8_t> bytes) noexcept {
    while (!bytes.empty()) {
        DWORD written = 0;
        const DWORD length = static_cast<DWORD>(std::min<size_t>(bytes.size(), MAXDWORD));
        if (!WriteFile(file, bytes.data(), length, &written, nullptr) || written == 0) return false;
        bytes = bytes.subspan(written);
    }
    return true;
}

uint64_t CacheFileTime(FILETIME time) noexcept {
    return static_cast<uint64_t>(time.dwHighDateTime) << 32 | time.dwLowDateTime;
}

void TouchDiskFile(const std::wstring& path) noexcept {
    CacheFileHandle file{CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    if (!file) return;
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(file.value, &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) return;
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    constexpr uint64_t minute = 60ull * 10000000;
    const uint64_t current = CacheFileTime(now), previous = CacheFileTime(info.ftLastWriteTime);
    if (current > previous && current - previous >= minute) SetFileTime(file.value, nullptr, nullptr, &now);
}

// Only canonical cache names in this dedicated directory are managed. Reserving
// space before staging also keeps our complete files plus the new temporary file
// within the budget. An open writer denies deletion of its own temporary file.
bool TrimDiskCache(const std::wstring& directory, uint64_t reserveBytes = 0, size_t reserveEntries = 0) {
    struct File { std::wstring name; uint64_t bytes; uint64_t used; };
    std::vector<File> files;
    uint64_t totalBytes = 0;
    WIN32_FIND_DATAW info{};
    const HANDLE search = FindFirstFileW((directory + L"\\*").c_str(), &info);
    if (search == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND;
    struct SearchHandle { HANDLE value; ~SearchHandle() { FindClose(value); } } guard{search};
    do {
        if (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) continue;
        const std::wstring_view name{info.cFileName};
        if (CacheTempName(name)) {
            DeleteFileW((directory + L"\\" + info.cFileName).c_str());
        } else if (CacheFileName(name)) {
            const uint64_t bytes = static_cast<uint64_t>(info.nFileSizeHigh) << 32 | info.nFileSizeLow;
            if (bytes > Detail::kBitmapCacheMaxBytes) {
                if (!DeleteFileW((directory + L"\\" + info.cFileName).c_str())) return false;
                continue;
            }
            files.push_back({info.cFileName, bytes, CacheFileTime(info.ftLastWriteTime)});
            totalBytes += bytes;
        }
    } while (FindNextFileW(search, &info));
    if (GetLastError() != ERROR_NO_MORE_FILES) return false;
    std::sort(files.begin(), files.end(), [](const File& a, const File& b) {
        return a.used != b.used ? a.used < b.used : a.name < b.name;
    });
    size_t count = files.size();
    for (const auto& file : files) {
        if (count + reserveEntries <= Detail::kBitmapCacheMaxEntries &&
            totalBytes + reserveBytes <= Detail::kBitmapCacheMaxBytes) break;
        if (DeleteFileW((directory + L"\\" + file.name).c_str())) {
            --count;
            totalBytes -= file.bytes;
        }
    }
    return count + reserveEntries <= Detail::kBitmapCacheMaxEntries &&
        totalBytes + reserveBytes <= Detail::kBitmapCacheMaxBytes;
}

std::optional<PreparedBitmap> LoadDiskBitmap(
        const std::wstring& directory, const Detail::BitmapDigest& source) {
    const std::wstring path = directory + L"\\" + DigestName(source) + L".bgra";
    CacheFileHandle file{CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    if (!file) return std::nullopt;
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(file.value, &info) || GetFileType(file.value) != FILE_TYPE_DISK ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) return std::nullopt;
    const uint64_t fileBytes = static_cast<uint64_t>(info.nFileSizeHigh) << 32 | info.nFileSizeLow;
    if (fileBytes < Detail::kBitmapCacheHeaderSize || fileBytes > Detail::kBitmapCacheMaxBytes) return std::nullopt;
    std::array<uint8_t, Detail::kBitmapCacheHeaderSize> header{};
    if (!ReadCacheBytes(file.value, header)) return std::nullopt;
    const auto parsed = Detail::ParseCacheHeader(header, fileBytes, source);
    if (!parsed) return std::nullopt;
    std::vector<uint8_t> pixels(static_cast<size_t>(fileBytes - header.size()));
    if (!ReadCacheBytes(file.value, pixels)) return std::nullopt;
    const auto checksum = BitmapSha256(std::span{header}.first(96), pixels);
    if (!checksum || !Detail::CacheChecksumMatches(header, *checksum)) return std::nullopt;
    const SoftwareBitmap bitmap(BitmapPixelFormat::Bgra8, static_cast<int32_t>(parsed->width),
        static_cast<int32_t>(parsed->height), BitmapAlphaMode::Premultiplied);
    bitmap.DpiX(parsed->dpiX);
    bitmap.DpiY(parsed->dpiY);
    {
        const auto buffer = bitmap.LockBuffer(BitmapBufferAccessMode::Write);
        const auto reference = buffer.CreateReference();
        uint8_t* output = nullptr;
        uint32_t capacity = 0;
        check_hresult(reference.as<::Windows::Foundation::IMemoryBufferByteAccess>()->GetBuffer(&output, &capacity));
        const auto plane = buffer.GetPlaneDescription(0);
        const Detail::BgraView input{pixels.data(), pixels.size(), 0,
            static_cast<int32_t>(parsed->width * 4), parsed->width, parsed->height};
        if (!Detail::CopyCroppedRows(input, {0, 0, parsed->width, parsed->height}, output,
                                     capacity, plane.StartIndex, plane.Stride)) return std::nullopt;
    }
    file.Close();
    TouchDiskFile(path);
    return PreparedBitmap{bitmap.GetReadOnlyView(),
        {static_cast<float>(parsed->width), static_cast<float>(parsed->height)}, source};
}

bool StoreDiskBitmap(const std::wstring& directory, const Detail::BitmapDigest& source,
                      const PreparedBitmap& prepared) {
    const Detail::CachedBitmapInfo info{static_cast<uint32_t>(prepared.bitmap.PixelWidth()),
        static_cast<uint32_t>(prepared.bitmap.PixelHeight()), prepared.bitmap.DpiX(),
        prepared.bitmap.DpiY(), source};
    if (!Detail::ValidCacheInfo(info)) return false;
    auto header = Detail::MakeCacheHeader(info);
    std::vector<uint8_t> pixels(static_cast<size_t>(info.width) * info.height * 4);
    {
        const auto buffer = prepared.bitmap.LockBuffer(BitmapBufferAccessMode::Read);
        const auto reference = buffer.CreateReference();
        uint8_t* input = nullptr;
        uint32_t capacity = 0;
        check_hresult(reference.as<::Windows::Foundation::IMemoryBufferByteAccess>()->GetBuffer(&input, &capacity));
        const auto plane = buffer.GetPlaneDescription(0);
        const Detail::BgraView view{input, capacity, plane.StartIndex, plane.Stride, info.width, info.height};
        if (!Detail::CopyCroppedRows(view, {0, 0, info.width, info.height}, pixels.data(), pixels.size(),
                                     0, static_cast<int32_t>(info.width * 4))) return false;
    }
    const auto checksum = BitmapSha256(std::span{header}.first(96), pixels);
    if (!checksum) return false;
    std::copy(checksum->begin(), checksum->end(), header.begin() + 96);
    if (!TrimDiskCache(directory, header.size() + pixels.size(), 1)) return false;
    std::array<uint8_t, 16> nonce{};
    if (BCryptGenRandom(nullptr, nonce.data(), static_cast<ULONG>(nonce.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        return false;
    const std::wstring path = directory + L"\\" + DigestName(source) + L".bgra";
    const std::wstring temp = path + L"." + DigestName(nonce) + L".tmp";
    struct TempFile { const std::wstring& path; ~TempFile() { DeleteFileW(path.c_str()); } } cleanup{temp};
    CacheFileHandle file{CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                    FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (!file || !WriteCacheBytes(file.value, header) || !WriteCacheBytes(file.value, pixels) ||
        !FlushFileBuffers(file.value)) return false;
    file.Close();
    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return false;
    return TrimDiskCache(directory);
}

std::optional<PreparedBitmap> TryLoadDiskBitmap(const Detail::BitmapDigest& source) noexcept {
    try {
        std::lock_guard lock(DiskCacheMutex());
        const auto directory = DiskCacheDirectory();
        if (directory.empty()) return std::nullopt;
        auto bitmap = LoadDiskBitmap(directory, source);
        TrimDiskCache(directory);
        return bitmap;
    } catch (...) { return std::nullopt; }
}

void TouchDiskBitmap(const Detail::BitmapDigest& source) noexcept {
    try {
        std::lock_guard lock(DiskCacheMutex());
        const auto directory = DiskCacheDirectory();
        if (!directory.empty()) TouchDiskFile(directory + L"\\" + DigestName(source) + L".bgra");
    } catch (...) {}
}

fire_and_forget PersistBitmapAsync(Detail::BitmapDigest source, PreparedBitmap prepared) {
    try {
        co_await resume_background();
        std::lock_guard lock(DiskCacheMutex());
        const auto directory = DiskCacheDirectory();
        if (!directory.empty()) StoreDiskBitmap(directory, source, prepared);
    } catch (...) {} // Optional cache writes must never turn image loading into a failure.
}

} // namespace

IAsyncOperation<Windows::Foundation::Size> DecodeCroppedAsync(
        std::vector<uint8_t> encoded,
        SoftwareBitmapSource const& destination) {
    if (encoded.empty() || !destination) co_return Windows::Foundation::Size{};

    const apartment_context caller;
    const SoftwareBitmapSource target = destination;
    co_await resume_background();

    if (const auto cached = FindPreparedBitmap(encoded)) {
        std::vector<uint8_t>{}.swap(encoded);
        co_await caller;
        co_await target.SetBitmapAsync(cached->bitmap);
        co_return cached->size;
    }

    PreparedBitmap prepared;
    const auto source = BitmapSha256(encoded);
    if (source) {
        if (const auto disk = TryLoadDiskBitmap(*source)) prepared = *disk;
    }
    if (!prepared.bitmap) {
        InMemoryRandomAccessStream stream;
        DataWriter writer(stream);
        writer.WriteBytes(encoded);
        co_await writer.StoreAsync();
        co_await writer.FlushAsync();
        writer.DetachStream();
        stream.Seek(0);

        const BitmapDecoder decoder = co_await BitmapDecoder::CreateAsync(stream);
        const SoftwareBitmap bitmap = co_await decoder.GetSoftwareBitmapAsync(
            BitmapPixelFormat::Bgra8, BitmapAlphaMode::Premultiplied, BitmapTransform{},
            ExifOrientationMode::IgnoreExifOrientation, ColorManagementMode::DoNotColorManage);
        prepared = CropBitmap(bitmap);
        prepared.diskSource = source;
        if (source) {
            try { PersistBitmapAsync(*source, prepared); } catch (...) {}
        }
    }
    prepared = RememberPreparedBitmap(std::move(encoded), std::move(prepared));
    co_await caller;
    co_await target.SetBitmapAsync(prepared.bitmap);
    co_return prepared.size;
}

} // namespace winrt::EGoTouchSettings::implementation::AccessoryImages
