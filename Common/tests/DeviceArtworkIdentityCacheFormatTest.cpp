#include "../../Tools/EGoTouchSettings/DeviceArtworkIdentityCacheFormat.h"
#include <iostream>
#include <stdexcept>

namespace Format = HuaweiHeadphones::Images::KnownDevices::Format;
using HuaweiHeadphones::Images::Identity;

static void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    try {
        const Identity pro{"00016D", "17"};
        const std::string row = "123456ABCDEF 00016D 17\n";
        const std::string canonical = std::string(Format::Header) + row;
        Check(row.size() == Format::RecordBytes, "record width");
        const auto parsed = Format::Parse(canonical);
        Check(parsed && parsed->size() == 1 && parsed->front() == Format::Record{0x123456abcdef, pro},
              "canonical identity read exactly");
        Check(Format::Encode(*parsed) == canonical, "canonical round trip");
        Check(Format::Parse(Format::Header) == Format::Records{}, "empty store allowed");
        Check(!Format::Parse(""), "missing version rejected");
        Check(!Format::Parse(std::string("X") + canonical), "wrong version rejected");
        Check(!Format::Parse(canonical.substr(0, canonical.size() - 1)), "truncated record rejected");
        Check(!Format::Parse(canonical + "x"), "trailing junk rejected");
        Check(!Format::Parse(std::string(Format::Header) + row + row), "duplicate address rejects whole store");
        Check(!Format::Parse(std::string(Format::Header) + "000000000000 00016D 17\n"), "zero address rejected");
        Check(!Format::Parse(std::string(Format::Header) + "1000000000000 00016D 17\n"), "address overflow rejected");
        Check(!Format::Parse(std::string(Format::Header) + "123456abcdef 00016D 17\n"), "lowercase address rejected");
        Check(!Format::Parse(std::string(Format::Header) + "123456ABCDEF 00016d 17\n"), "lowercase resource rejected");
        Check(!Format::Parse(std::string(Format::Header) + "123456ABCDEF 00016D 1f\n"), "lowercase color rejected");
        Check(!Format::Parse(std::string(Format::Header) + "123456ABCDEF 000000 17\n"), "empty resource rejected");
        Check(!Format::Parse(std::string(Format::Header) + "123456ABCDEF 00000A 17\n"), "generic fallback rejected");
        Check(!Format::Parse(std::string(Format::Header) + "123456ABCDEF ../16D 17\n"), "path injection rejected");
        Check(!Format::Parse(std::string(Format::Header) + "123456ABCDEF 00016D /7\n"), "color injection rejected");
        Check(!Format::Parse(std::string(Format::Header) + "123456ABCDEF\t00016D 17\n"), "noncanonical delimiter rejected");
        Check(!Format::Parse(canonical.substr(0, canonical.size() - 1) + "\r\n"), "unexpected carriage return rejected");
        auto embeddedNull = canonical;
        embeddedNull[Format::Header.size() + 14] = '\0';
        Check(!Format::Parse(embeddedNull), "embedded null rejected");
        Check(!Format::Parse(canonical + std::string(Format::MaxFileBytes, 'X')), "file byte cap enforced");
        Check(!Format::Parse(canonical + "000000000001 000000 01\n"), "bad later row invalidates earlier row");

        Check(!Format::Encode(Format::Records{{0, pro}}), "encoder rejects zero address");
        Check(!Format::Encode(Format::Records{{uint64_t{1} << 48, pro}}), "encoder rejects overflowing address");
        Check(!Format::Encode(Format::Records{{1, pro}, {1, pro}}), "encoder rejects duplicate address");
        Check(!Format::Encode(Format::Records{{1, {"00016D", ""}}}), "encoder rejects absent color");

        Format::Records records;
        for (uint64_t address = 1; address <= Format::MaxEntries; ++address)
            Check(Format::Promote(records, address, pro), "new verified device stored");
        Check(records.size() == 8 && records.front().address == 8 && records.back().address == 1,
              "records ordered newest first");
        const auto fullStore = Format::Encode(records);
        Check(fullStore && fullStore->size() == Format::MaxFileBytes, "eight record exact byte limit");
        Check(!Format::Parse(*fullStore + "000000000009 00016D 17\n"), "ninth record rejected");
        Check(!Format::Encode(Format::Records(9, {1, pro})), "encoder enforces eight record cap");
        Check(!Format::Promote(records, 8, pro) && Format::Encode(records) == fullStore,
              "same newest identity causes no write");
        Check(Format::Promote(records, 9, pro), "ninth verified device admitted");
        Check(records.size() == 8 && records.front().address == 9 && records.back().address == 2,
              "least recently confirmed device evicted");
        Check(Format::Promote(records, 2, pro) && records.front().address == 2,
              "returning device promoted without duplicate");
        Check(Format::Promote(records, 2, Identity{"00016D", "01"}) && records.front().identity.colorId == "01",
              "fresh verified color replaces old color");
        const auto beforeInvalid = records;
        Check(!Format::Promote(records, 0, pro) && !Format::Promote(records, 10, Identity{"000000", "17"}) &&
              records == beforeInvalid, "invalid input does not mutate records");
        Format::Records sequence;
        Check(Format::Promote(sequence, 1, pro) && Format::Promote(sequence, 2, pro) &&
              Format::Promote(sequence, 1, pro), "A-B-A updates recency");
        Check(sequence.size() == 2 && sequence[0].address == 1 && sequence[1].address == 2,
              "A-B-A keeps unique addresses in recency order");
        Check(Format::Promote(sequence, sequence.back().address, sequence.back().identity) &&
              sequence.front() == Format::Record{2, pro}, "promotion permits an identity reference into records");
        std::cout << "Device artwork identity cache format and bounded recency passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
