#include "DeviceArtworkIdentityCache.h"
#include "DeviceArtworkIdentityCacheFormat.h"
#include "DeviceArtworkIdentityCacheStore.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")

namespace HuaweiHeadphones::Images::KnownDevices {
namespace {
namespace fs = std::filesystem;
constexpr wchar_t kIndexName[] = L"devices-v1.txt";
constexpr wchar_t kLockName[] = L"devices-v1.lock";
std::mutex g_storeMutex;
std::atomic<uint64_t> g_tempSequence{0};

struct FileHandle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit FileHandle(HANDLE handle) : value(handle) {}
    ~FileHandle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;
    explicit operator bool() const { return value != INVALID_HANDLE_VALUE; }
};

struct WriterLock {
    FileHandle file;
    OVERLAPPED position{};
    bool locked = false;
    explicit WriterLock(const fs::path& directory)
        : file(CreateFileW((directory / kLockName).c_str(), GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)) {
        if (!file) return;
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(file.value, &info) ||
            (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) return;
        locked = LockFileEx(file.value, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
                            0, 1, 0, &position) != FALSE;
    }
    ~WriterLock() { if (locked) UnlockFileEx(file.value, 0, 1, 0, &position); }
};

fs::path CacheDirectory() {
    PWSTR local = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DONT_VERIFY,
                                    nullptr, &local))) {
        if (local) CoTaskMemFree(local);
        return {};
    }
    try {
        fs::path result = fs::path(local) / L"OpenEGoHub" / L"Cache" / L"Artwork";
        CoTaskMemFree(local);
        return result;
    } catch (...) {
        CoTaskMemFree(local);
        throw;
    }
}

std::optional<std::string> ReadIndex(const fs::path& directory) {
    FileHandle file(CreateFileW((directory / kIndexName).c_str(), GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING,
                               FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (!file) return std::nullopt;
    BY_HANDLE_FILE_INFORMATION info{};
    LARGE_INTEGER length{};
    if (!GetFileInformationByHandle(file.value, &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        !GetFileSizeEx(file.value, &length) || length.QuadPart < 0 ||
        uint64_t(length.QuadPart) > Format::MaxFileBytes) return std::nullopt;
    std::string bytes(static_cast<size_t>(length.QuadPart), '\0');
    DWORD read = 0;
    if (!ReadFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) ||
        read != bytes.size()) return std::nullopt;
    char extra = 0;
    if (!ReadFile(file.value, &extra, 1, &read, nullptr) || read != 0) return std::nullopt;
    return bytes;
}

bool ReplaceIndex(const fs::path& directory, const std::string& bytes) {
    fs::path temporary;
    bool written = false;
    bool ownsTemporary = false;
    // CREATE_NEW never opens an older temporary file, including after a crash.
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        const auto suffix = std::to_wstring(GetCurrentProcessId()) + L"-" +
            std::to_wstring(GetCurrentThreadId()) + L"-" +
            std::to_wstring(g_tempSequence.fetch_add(1, std::memory_order_relaxed));
        temporary = directory / (L"devices-v1-" + suffix + L".tmp");
        FileHandle file(CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
                                   CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
                                   nullptr));
        if (!file) {
            if (GetLastError() == ERROR_FILE_EXISTS || GetLastError() == ERROR_ALREADY_EXISTS) continue;
            return false;
        }
        ownsTemporary = true;
        DWORD count = 0;
        written = WriteFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()),
                            &count, nullptr) && count == bytes.size() && FlushFileBuffers(file.value);
        break;
    }
    if (written) {
        const auto target = directory / kIndexName;
        if (ReplaceFileW(target.c_str(), temporary.c_str(), nullptr,
                         REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr)) return true;
        // The first write has no destination to replace. Never remove a newer
        // index on a failed replace; all cooperating writers hold kLockName.
        if (GetLastError() == ERROR_FILE_NOT_FOUND &&
            MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH)) return true;
    }
    const DWORD error = GetLastError();
    if (ownsTemporary) DeleteFileW(temporary.c_str());
    SetLastError(error);
    return false;
}
}

namespace Detail {
std::optional<Identity> LoadFromDirectory(const fs::path& directory, uint64_t address) noexcept {
    if (directory.empty() || !Format::ValidAddress(address)) return std::nullopt;
    try {
        // Windows replacement can briefly remove the old directory entry. Keep
        // same-process readers outside that interval without writing on Load.
        std::lock_guard guard(g_storeMutex);
        const auto bytes = ReadIndex(directory);
        if (!bytes) return std::nullopt;
        const auto records = Format::Parse(*bytes);
        if (!records) return std::nullopt;
        for (const auto& record : *records)
            if (record.address == address && ValidFields(record.identity)) return record.identity;
    } catch (...) {}
    return std::nullopt;
}

bool RememberInDirectory(const fs::path& directory, uint64_t address, const Identity& identity) noexcept {
    if (directory.empty() || !Format::ValidAddress(address) || !ValidFields(identity)) return false;
    try {
        std::lock_guard guard(g_storeMutex);
        std::error_code error;
        fs::create_directories(directory, error);
        if (error || !fs::is_directory(directory, error) || error) return false;
        WriterLock lock(directory);
        if (!lock.locked) return false;
        Format::Records records;
        if (auto bytes = ReadIndex(directory)) {
            // A corrupt/duplicate/out-of-bounds index supplies no trusted hints.
            if (auto parsed = Format::Parse(*bytes)) records = std::move(*parsed);
        }
        if (!Format::Promote(records, address, identity)) return true;
        const auto encoded = Format::Encode(records);
        return encoded && ReplaceIndex(directory, *encoded);
    } catch (...) {
        return false;
    }
}
}

std::optional<Identity> Load(uint64_t address) noexcept {
    if (!Format::ValidAddress(address)) return std::nullopt;
    try { return Detail::LoadFromDirectory(CacheDirectory(), address); }
    catch (...) { return std::nullopt; }
}

void Remember(uint64_t address, const Identity& identity) noexcept {
    if (!Format::ValidAddress(address) || !ValidFields(identity)) return;
    try { (void)Detail::RememberInDirectory(CacheDirectory(), address, identity); }
    catch (...) {}
}
}
