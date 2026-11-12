#include "../../Tools/EGoTouchSettings/DeviceArtworkIdentityCacheStore.h"
#include "../../Tools/EGoTouchSettings/DeviceArtworkIdentityCacheFormat.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace Store = HuaweiHeadphones::Images::KnownDevices::Detail;
namespace Format = HuaweiHeadphones::Images::KnownDevices::Format;
namespace fs = std::filesystem;
using HuaweiHeadphones::Images::Identity;

static void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct TemporaryDirectory {
    fs::path path = fs::temp_directory_path() /
        (L"OpenEGoHubArtworkCacheStoreTest-" + std::to_wstring(GetCurrentProcessId()) +
         L"-" + std::to_wstring(GetTickCount64()));
    TemporaryDirectory() { Check(!fs::exists(path), "unique temporary directory must be absent"); }
    ~TemporaryDirectory() {
        // Never recurse or remove anything outside this test's two known files.
        std::error_code ignored;
        fs::remove(path / L"devices-v1.txt", ignored);
        fs::remove(path / L"devices-v1.lock", ignored);
        fs::remove(path, ignored);
    }
};

struct Handle {
    HANDLE value;
    explicit Handle(HANDLE handle) : value(handle) {}
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};

static std::string ReadBytes(const fs::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    Check(bool(input), "read isolated index");
    const auto size = input.tellg();
    Check(size >= 0, "isolated index size");
    std::string bytes(static_cast<size_t>(size), '\0');
    input.seekg(0);
    Check(bool(input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))), "read complete isolated index");
    return bytes;
}

static void WriteBytes(const fs::path& path, std::string_view bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    Check(bool(output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()))), "write isolated fixture");
    output.close();
    Check(bool(output), "close isolated fixture");
}

int main() {
    try {
        TemporaryDirectory temporary;
        const auto& directory = temporary.path;
        const auto index = directory / L"devices-v1.txt";
        const auto lockPath = directory / L"devices-v1.lock";
        const Identity pro{"00016D", "17"};
        Check(!Store::LoadFromDirectory(directory, 1) && !fs::exists(directory),
              "missing cache lookup must not create a directory");
        Check(Store::RememberInDirectory(directory, 1, pro), "first verified identity persisted");
        Check(Store::LoadFromDirectory(directory, 1) == pro, "persisted identity loaded exactly");
        const auto initialBytes = ReadBytes(index);
        // A deliberately older timestamp catches a rewrite even on coarse clocks.
        fs::last_write_time(index, fs::last_write_time(index) - std::chrono::hours(24));
        const auto unchangedTime = fs::last_write_time(index);
        Check(Store::RememberInDirectory(directory, 1, pro), "duplicate confirmation succeeds");
        Check(ReadBytes(index) == initialBytes && fs::last_write_time(index) == unchangedTime,
              "identical newest identity must not rewrite the index");
        Check(!Store::RememberInDirectory(directory, 0, pro) &&
              !Store::RememberInDirectory(directory, uint64_t{1} << 48, pro) &&
              !Store::RememberInDirectory(directory, 2, Identity{"000000", "17"}) &&
              !Store::RememberInDirectory(directory, 2, Identity{"00016D", "../"}),
              "invalid addresses and identities rejected");
        Check(ReadBytes(index) == initialBytes && fs::last_write_time(index) == unchangedTime,
              "invalid input leaves existing index untouched");

        for (uint64_t address = 2; address <= 8; ++address)
            Check(Store::RememberInDirectory(directory, address, pro), "eight verified devices stored");
        Check(Store::RememberInDirectory(directory, 9, pro), "ninth device stored");
        Check(!Store::LoadFromDirectory(directory, 1), "least recently confirmed device evicted");
        for (uint64_t address = 2; address <= 9; ++address)
            Check(Store::LoadFromDirectory(directory, address) == pro, "latest eight devices retained");
        Check(Store::RememberInDirectory(directory, 2, pro) && Store::RememberInDirectory(directory, 10, pro),
              "returning device promoted before next insertion");
        Check(Store::LoadFromDirectory(directory, 2) == pro && !Store::LoadFromDirectory(directory, 3),
              "promotion changes eviction order");

        const std::string row = "000000000002 00016D 17\n";
        const std::array corrupt{
            std::string(Format::Header) + row + row,
            std::string(Format::Header) + row + "000000000003 000000 17\n",
            std::string(Format::Header) + row.substr(0, row.size() - 1),
            std::string(Format::Header) + std::string(Format::MaxFileBytes, 'X')};
        for (const auto& fixture : corrupt) {
            WriteBytes(index, fixture);
            Check(!Store::LoadFromDirectory(directory, 2), "any corrupt or duplicate file rejects all hints");
        }
        Check(Store::RememberInDirectory(directory, 55, pro), "verified identity repairs corrupt store");
        const auto repaired = Format::Parse(ReadBytes(index));
        Check(repaired && repaired->size() == 1 && repaired->front() == Format::Record{55, pro} &&
              !Store::LoadFromDirectory(directory, 2), "repair trusts only the new verified identity");

        {
            Handle lock(CreateFileW(lockPath.c_str(), GENERIC_READ | GENERIC_WRITE,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
            Check(lock.value != INVALID_HANDLE_VALUE, "open isolated writer lock");
            OVERLAPPED position{};
            Check(LockFileEx(lock.value, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
                             0, 1, 0, &position) != FALSE, "hold exclusive isolated writer lock");
            const auto beforeLockedWrite = ReadBytes(index);
            const auto started = std::chrono::steady_clock::now();
            const bool written = Store::RememberInDirectory(directory, 66, pro);
            const auto elapsed = std::chrono::steady_clock::now() - started;
            Check(!written && elapsed < std::chrono::seconds(1), "contended file lock fails without waiting");
            Check(ReadBytes(index) == beforeLockedWrite, "contended writer leaves index intact");
            Check(UnlockFileEx(lock.value, 0, 1, 0, &position) != FALSE, "release isolated writer lock");
            Check(Store::RememberInDirectory(directory, 66, pro), "write succeeds after unlock");
        }

        WriteBytes(index, Format::Header);
        std::atomic<bool> start{false};
        std::atomic<bool> writersSucceeded{true};
        std::array<std::thread, 8> writers;
        for (size_t i = 0; i < writers.size(); ++i) {
            writers[i] = std::thread([&, address = uint64_t(i + 1)] {
                while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
                if (!Store::RememberInDirectory(directory, address, pro)) writersSucceeded = false;
            });
        }
        start.store(true, std::memory_order_release);
        for (auto& writer : writers) writer.join();
        Check(writersSucceeded, "all concurrent same-process writers succeeded");
        for (uint64_t address = 1; address <= 8; ++address)
            Check(Store::LoadFromDirectory(directory, address) == pro, "concurrent writers merge all eight devices");
        const auto merged = Format::Parse(ReadBytes(index));
        Check(merged && merged->size() == 8, "concurrent merge produces one complete bounded store");

        std::atomic<bool> done{false};
        std::atomic<bool> readerSucceeded{true};
        std::atomic<unsigned> observations{0};
        std::atomic<unsigned> missingIdentities{0};
        std::atomic<DWORD> firstReadError{ERROR_SUCCESS};
        std::thread reader([&] {
            while (!done.load(std::memory_order_acquire)) {
                const auto observed = Store::LoadFromDirectory(directory, 1);
                if (!observed) {
                    const DWORD error = GetLastError();
                    if (missingIdentities.fetch_add(1) == 0) firstReadError = error;
                }
                if (!observed || observed->modelId != "00016D" ||
                    (observed->colorId != "17" && observed->colorId != "01")) readerSucceeded = false;
                observations.fetch_add(1, std::memory_order_relaxed);
                std::this_thread::yield();
            }
        });
        bool atomicWritesSucceeded = true;
        DWORD firstWriteError = ERROR_SUCCESS;
        for (unsigned i = 0; i < 40; ++i) {
            if (!Store::RememberInDirectory(directory, 1,
                    Identity{"00016D", i % 2 == 0 ? "01" : "17"})) {
                if (atomicWritesSucceeded) firstWriteError = GetLastError();
                atomicWritesSucceeded = false;
            }
        }
        done.store(true, std::memory_order_release);
        reader.join();
        if (!atomicWritesSucceeded || !readerSucceeded || observations == 0) {
            std::cerr << "Atomic write success=" << atomicWritesSucceeded
                      << " read success=" << readerSucceeded << " observations=" << observations
                      << " missing=" << missingIdentities << " first read error=" << firstReadError
                      << " first write error=" << firstWriteError << '\n';
        }
        Check(atomicWritesSucceeded && readerSucceeded && observations != 0,
              "readers see complete old or new identities throughout atomic replacement");
        std::cout << "Device artwork identity cache Windows store, locking and atomic replacement passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
