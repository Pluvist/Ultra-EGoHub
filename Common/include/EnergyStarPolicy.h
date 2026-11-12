#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace EnergyStarPolicy {
namespace Detail {
constexpr wchar_t FoldAscii(wchar_t value) noexcept {
    return value >= L'A' && value <= L'Z' ? wchar_t(value + (L'a' - L'A')) : value;
}
inline std::wstring_view Basename(std::wstring_view path) noexcept {
    const auto slash = path.find_last_of(L"/\\");
    return path.substr(slash == std::wstring_view::npos ? 0 : slash + 1);
}
inline bool EqualName(std::wstring_view first, std::wstring_view second) noexcept {
    if (first.size() != second.size()) return false;
    for (size_t i = 0; i < first.size(); ++i)
        if (FoldAscii(first[i]) != FoldAscii(second[i])) return false;
    return true;
}
inline bool StartsWithName(std::wstring_view value, std::wstring_view prefix) noexcept {
    return value.size() >= prefix.size() && EqualName(value.substr(0, prefix.size()), prefix);
}
inline std::wstring_view ExecutableStem(std::wstring_view image) noexcept {
    auto name = Basename(image);
    if (name.size() >= 4 && EqualName(name.substr(name.size() - 4), L".exe")) name.remove_suffix(4);
    return name;
}
inline bool IsAmbientShell(std::wstring_view image) noexcept {
    const auto stem = ExecutableStem(image);
    static constexpr std::array names{
        L"explorer", L"dwm", L"sihost", L"shellexperiencehost", L"startmenuexperiencehost",
        L"searchhost", L"winlogon", L"csrss", L"svchost"};
    for (const auto name : names) if (EqualName(stem, name)) return true;
    return false;
}
}

// EnergyStar's complete original bypass list, including its debug-only devenv
// entry in every build here. Match executable basenames, never directory names.
inline bool IsBypassed(std::wstring_view pathOrName) noexcept {
    if (pathOrName.find(L'\0') != std::wstring_view::npos) return true;
    const auto name = Detail::Basename(pathOrName);
    if (name.empty()) return true;
    auto stem = name;
    if (stem.size() >= 4 && Detail::EqualName(stem.substr(stem.size() - 4), L".exe"))
        stem.remove_suffix(4);
    static constexpr std::array upstream{
        L"energystar", L"msedge", L"webviewhost", L"applicationframehost",
        L"taskmgr", L"procmon", L"procmon64", L"widgets", L"dwm", L"explorer",
        L"shellexperiencehost", L"startmenuexperiencehost", L"searchhost", L"sihost",
        L"fontdrvhost", L"chsime", L"ctfmon", L"devenv", L"csrss", L"smss",
        L"svchost", L"wudfrd"};
    for (const auto bypass : upstream) if (Detail::EqualName(stem, bypass)) return true;

    // Preserve the established touch scheduling and every Hub/HAL companion,
    // including old, debug and diagnostic names. A foreground root is never an
    // exemption from this rule: native writes must still call IsBypassed.
    for (const auto prefix : {L"egotouch", L"openego", L"gaokun", L"huawei"})
        if (Detail::StartsWithName(stem, prefix)) return true;
    static constexpr std::array inputAndVendor{
        L"btmcutesttool", L"tsarawreplay", L"chiphidprobeb", L"himaxthpprobe",
        L"thp_service", L"thpservice", L"penservice", L"keyboardservice",
        L"hw_osdserver", L"hwveaudioservice", L"hwpcmanager", L"hwpccoreservice",
        L"hwdistributedmainservice", L"hiconnectivityservice", L"lcd_service",
        L"mbamainservice", L"matebookservice", L"mousecrossingdaemon", L"wucsproxy",
        // Windows input/audio helpers must not undermine pen, touch or audio.
        L"textinputhost", L"tabtip", L"audiodg", L"system", L"registry",
        L"idle", L"memory compression", L"winlogon", L"wininit", L"lsass", L"services"};
    for (const auto bypass : inputAndVendor) if (Detail::EqualName(stem, bypass)) return true;
    return false;
}

struct ForegroundSelection {
    bool valid = false;
    bool frame = false;
    std::vector<uint32_t> roots;
};

// Unknown foreground identity is different from a known ambient shell. A shell
// is valid but supplies no family root, preventing Explorer's process tree from
// protecting every application in the session. UWP frames require real children.
inline ForegroundSelection ResolveForeground(uint32_t pid, std::wstring_view image,
                                             std::span<const uint32_t> childPids = {}) {
    ForegroundSelection selection;
    if (!pid || image.empty() || Detail::Basename(image).empty() ||
        image.find(L'\0') != std::wstring_view::npos) return selection;
    if (Detail::EqualName(Detail::ExecutableStem(image), L"applicationframehost")) {
        selection.frame = true;
        for (const auto child : childPids)
            if (child && child != pid) selection.roots.push_back(child);
        std::sort(selection.roots.begin(), selection.roots.end());
        selection.roots.erase(std::unique(selection.roots.begin(), selection.roots.end()), selection.roots.end());
        selection.valid = !selection.roots.empty();
        return selection;
    }
    selection.valid = true;
    if (!Detail::IsAmbientShell(image)) selection.roots.push_back(pid);
    return selection;
}

struct PowerState {
    uint32_t controlMask = 0;
    uint32_t stateMask = 0;
    bool operator==(const PowerState&) const = default;
};
inline constexpr uint32_t ExecutionSpeedMask = 1;
inline constexpr PowerState EnableEco(PowerState original) noexcept {
    return {original.controlMask | ExecutionSpeedMask, original.stateMask | ExecutionSpeedMask};
}
inline constexpr PowerState RestoreExecution(PowerState current, PowerState original) noexcept {
    return {(current.controlMask & ~ExecutionSpeedMask) | (original.controlMask & ExecutionSpeedMask),
            (current.stateMask & ~ExecutionSpeedMask) | (original.stateMask & ExecutionSpeedMask)};
}

struct ProcessNode {
    uint32_t pid;
    uint32_t parent;
    std::wstring image;
};

// Return sorted unique roots and descendants. Same-image ordinary ancestors
// may join the family, then their descendants (for multi-process applications).
// Stop before an unrelated/bypassed ancestor such as Explorer or a system host.
// This is a foreground protection set, not permission to alter bypassed PIDs.
inline std::vector<uint32_t> ForegroundFamily(
        std::span<const ProcessNode> processes, std::span<const uint32_t> foregroundRoots) {
    std::unordered_map<uint32_t, const ProcessNode*> nodes;
    nodes.reserve(processes.size());
    for (const auto& process : processes)
        if (process.pid != 0) nodes.try_emplace(process.pid, &process);
    std::unordered_map<uint32_t, std::vector<uint32_t>> children;
    children.reserve(nodes.size());
    for (const auto& [pid, process] : nodes)
        if (process->parent != 0) children[process->parent].push_back(pid);
    std::unordered_set<uint32_t> selected;
    selected.reserve(nodes.size());
    std::vector<uint32_t> family;
    family.reserve(nodes.size());
    const auto add = [&selected, &family](uint32_t pid) {
        if (pid == 0 || !selected.insert(pid).second) return false;
        family.push_back(pid);
        return true;
    };
    for (const auto root : foregroundRoots) {
        if (!add(root)) continue;
        auto current = nodes.find(root);
        while (current != nodes.end() && !IsBypassed(current->second->image)) {
            const auto parent = nodes.find(current->second->parent);
            if (parent == nodes.end() || IsBypassed(parent->second->image) ||
                !Detail::EqualName(Detail::Basename(current->second->image),
                                   Detail::Basename(parent->second->image))) break;
            if (!add(parent->first)) break; // also bounds self-parent and cycles
            current = parent;
        }
    }
    for (size_t next = 0; next < family.size(); ++next) {
        if (const auto found = children.find(family[next]); found != children.end())
            for (const auto child : found->second) add(child);
    }
    std::sort(family.begin(), family.end());
    return family;
}
}
