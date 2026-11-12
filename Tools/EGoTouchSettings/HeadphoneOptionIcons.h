#pragma once
#include <string_view>

// Segoe Fluent Icons names/code points verified against Microsoft's font table:
// https://learn.microsoft.com/windows/apps/design/iconography/segoe-fluent-icons-font
// Presentation only: identifiers, protocol controls and their behavior are unchanged.
namespace HeadphoneOptionIcons {

struct Entry {
    std::string_view id;
    wchar_t const* glyph;
};

// Every ControlProfile identifier is explicit, including generated side/gesture
// identifiers. Left/right labels carry the side; both sides use the same gesture
// symbol rather than an arrow that could imply a different gesture direction.
inline constexpr Entry kControls[]{
    {"noise",           L"\xE7F6"}, // Headphone: complete-size noise-control symbol
    {"anc_direction",   L"\xE7AD"}, // Rotate: adjustable noise-cancellation direction
    {"anc_level",       L"\xEBC5"}, // VolumeBars: noise-cancellation strength
    {"transparency",    L"\xF270"}, // Ear: hearing surrounding sound
    {"wear",            L"\xF270"}, // Ear: wearing detection
    {"latency",         L"\xEC4A"}, // SpeedHigh: low latency
    {"quality",         L"\xEC4F"}, // MusicNote: high-quality music
    {"adaptive_volume", L"\xE767"}, // Volume: automatic volume adjustment
    {"dual",            L"\xE975"}, // Devices2: two connected devices
    {"case_sound",      L"\xEA8F"}, // Ringer: case notification sound
    {"drop",            L"\xE7BA"}, // Warning: dropping alert
    {"head",            L"\xE8B4"}, // Orientation: head movement
    {"single_anc",      L"\xF4C0"}, // Earbud: one-ear noise cancellation
    {"wind",            L"\xEBE6"}, // WindDirection: wind-noise reduction
    {"voice",           L"\xE720"}, // Microphone: voice control
    {"tips",            L"\xF4C0"}, // Earbud: ear-tip material
    {"noisy_volume",    L"\xE995"}, // Volume3: louder audio in noisy surroundings
    {"spatial",         L"\xF0EE"}, // SpatialVolume3: spatial audio
    {"scene",           L"\xE714"}, // Video: spatial-audio entertainment scene
    {"equalizer",       L"\xE9E9"}, // Equalizer: sound presets
    {"double1",         L"\xE7C9"}, // TouchPointer: complete-size tap symbol, left
    {"double2",         L"\xE7C9"}, // TouchPointer: complete-size tap symbol, right
    {"triple1",         L"\xE7C9"}, // TouchPointer: adjacent label carries the tap count
    {"triple2",         L"\xE7C9"}, // TouchPointer: adjacent label carries the tap count
    {"hold1",           L"\xE815"}, // Touch: complete-size held-touch symbol, left
    {"hold2",           L"\xE815"}, // Touch: complete-size held-touch symbol, right
    {"swipe1",          L"\xE927"}, // Swipe: complete-size slide symbol, left
    {"swipe2",          L"\xE927"}, // Swipe: complete-size slide symbol, right
    {"pinch0",          L"\xE80B"}, // CallControl: answer/end call
    {"pinch1",          L"\xF405"}, // DeclineCall: reject call
    {"pinch2",          L"\xE768"}, // Play: play/pause gesture
    {"pinch3",          L"\xE893"}, // Next: next track
    {"pinch4",          L"\xE892"}, // Previous: previous track
    {"double_enabled",  L"\xE768"}, // Play: enable double-tap playback control
    {"swipe_volume",    L"\xEC8F"}, // ScrollUpDown: slide volume control
    {"nod",             L"\xE70D"}, // ChevronDown: downward nod
    {"shake",           L"\xE7AD"}, // Rotate: turn head to either side
};

inline constexpr Entry kPageIcons[]{
    {"group_sound",      L"\xE767"}, // Volume
    {"group_usage",      L"\xE713"}, // Settings
    {"group_gestures",   L"\xE815"}, // Touch
    {"custom_equalizer", L"\xE9E9"}, // Equalizer
    {"paired_peers",     L"\xE975"}, // Devices2
    {"refresh",          L"\xE72C"}, // Refresh
    {"reconnect",        L"\xE71B"}, // Link
    {"fast_reconnect",   L"\xE895"}, // Sync
    {"left_gestures",    L"\xE815"}, // Touch; the adjacent label identifies left
    {"right_gestures",   L"\xE815"}, // Touch; the adjacent label identifies right
    {"save",             L"\xE74E"}, // Save
    {"remove_peer",      L"\xE738"}, // Remove
};

inline constexpr wchar_t const* Glyph(std::string_view id) noexcept {
    for (auto const& entry : kControls) if (entry.id == id) return entry.glyph;
    for (auto const& entry : kPageIcons) if (entry.id == id) return entry.glyph;
    return L"\xE7F6"; // Headphone: future/unknown headphone option
}

inline constexpr wchar_t const* GroupGlyph(std::wstring_view group) noexcept {
    if (group == L"声音") return Glyph("group_sound");
    if (group == L"手势") return Glyph("group_gestures");
    return Glyph("group_usage");
}

} // namespace HeadphoneOptionIcons
