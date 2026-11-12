#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>

namespace HeadphoneAudioReconnect {

enum class Status { Requested, AlreadyConnected, NotPaired, Unsupported, Failed };
struct Result {
    Status status;
    HRESULT error;
};

// Run on the caller's background worker. This performs at most one audio-driver
// reconnect request for an exact, revalidated paired address. Requested means
// only that the driver accepted an attempt; connection confirmation is separate.
Result Request(uint64_t pairedAddress);

} // namespace HeadphoneAudioReconnect
