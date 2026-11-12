#pragma once
#include "HeadphoneImageIdentity.h"
#include <utility>
#include <vector>

namespace HuaweiHeadphones::Images::KnownDevices::Format {

inline constexpr std::string_view Header = "OpenEGoHubArtworkDevices-v1\n";
inline constexpr size_t MaxEntries = 8;
inline constexpr size_t RecordBytes = 23;
inline constexpr size_t MaxFileBytes = Header.size() + MaxEntries * RecordBytes;

struct Record {
    uint64_t address;
    Identity identity;
    bool operator==(const Record&) const = default;
};
using Records = std::vector<Record>;

inline bool ValidAddress(uint64_t address) {
    return address != 0 && (address >> 48) == 0;
}

inline bool ValidRecords(std::span<const Record> records) {
    if (records.size() > MaxEntries) return false;
    for (size_t i = 0; i < records.size(); ++i) {
        if (!ValidAddress(records[i].address) || !ValidFields(records[i].identity)) return false;
        for (size_t previous = 0; previous < i; ++previous)
            if (records[previous].address == records[i].address) return false;
    }
    return true;
}

// The entire file is untrusted if any record is malformed or duplicated. Fixed
// widths and a small byte cap keep parsing bounded before allocating records.
inline std::optional<Records> Parse(std::string_view bytes) {
    if (bytes.size() > MaxFileBytes || !bytes.starts_with(Header)) return std::nullopt;
    bytes.remove_prefix(Header.size());
    if (bytes.size() % RecordBytes != 0) return std::nullopt;
    Records records;
    records.reserve(bytes.size() / RecordBytes);
    while (!bytes.empty()) {
        const auto row = bytes.substr(0, RecordBytes);
        if (!IsUpperHex(row.substr(0, 12), 12) || row[12] != ' ' ||
            row[19] != ' ' || row[22] != '\n') return std::nullopt;
        uint64_t address = 0;
        for (char digit : row.substr(0, 12)) {
            address = (address << 4) | uint64_t(digit <= '9' ? digit - '0' : digit - 'A' + 10);
        }
        records.push_back({address, {std::string(row.substr(13, 6)), std::string(row.substr(20, 2))}});
        bytes.remove_prefix(RecordBytes);
    }
    if (!ValidRecords(records)) return std::nullopt;
    return records;
}

inline std::optional<std::string> Encode(std::span<const Record> records) {
    if (!ValidRecords(records)) return std::nullopt;
    static constexpr std::string_view hex = "0123456789ABCDEF";
    std::string bytes(Header);
    bytes.reserve(Header.size() + records.size() * RecordBytes);
    for (const auto& record : records) {
        for (int digit = 11; digit >= 0; --digit)
            bytes.push_back(hex[(record.address >> (digit * 4)) & 15]);
        bytes.push_back(' ');
        bytes += record.identity.modelId;
        bytes.push_back(' ');
        bytes += record.identity.colorId;
        bytes.push_back('\n');
    }
    return bytes;
}

// Called after a freshly verified device identity, rather than on cache loads.
// The front is newest; repeated confirmations of that exact entry do not write.
inline bool Promote(Records& records, uint64_t address, const Identity& identity) {
    if (!ValidAddress(address) || !ValidFields(identity) || !ValidRecords(records)) return false;
    auto found = std::find_if(records.begin(), records.end(), [address](const auto& record) {
        return record.address == address;
    });
    if (found == records.begin() && found != records.end() && found->identity == identity) return false;
    // The caller may pass an identity already contained in records.
    Record promoted{address, identity};
    if (found != records.end()) records.erase(found);
    records.insert(records.begin(), std::move(promoted));
    if (records.size() > MaxEntries) records.pop_back();
    return true;
}

}
