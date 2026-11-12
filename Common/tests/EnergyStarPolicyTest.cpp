#include "EnergyStarPolicy.h"
#include <iostream>
#include <stdexcept>

namespace Policy = EnergyStarPolicy;
using Policy::PowerState;
using Policy::ProcessNode;

static void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

static void Family(const std::vector<ProcessNode>& processes, std::initializer_list<uint32_t> roots,
                   std::initializer_list<uint32_t> expected, const char* message) {
    Check(Policy::ForegroundFamily(processes, std::span(roots.begin(), roots.size())) ==
          std::vector<uint32_t>(expected), message);
}

static_assert(Policy::EnableEco(PowerState{4, 2}) == PowerState{5, 3});
static_assert(Policy::RestoreExecution(PowerState{7, 4}, PowerState{0, 1}) == PowerState{6, 5});

int main() {
    try {
        // Independent copy of the complete upstream requirement, including its
        // DEBUG entry: every one must remain protected in a Release build.
        constexpr std::array upstream{
            L"EnergyStar.exe", L"msedge.exe", L"WebViewHost.exe", L"ApplicationFrameHost.exe",
            L"taskmgr.exe", L"procmon.exe", L"procmon64.exe", L"Widgets.exe", L"dwm.exe",
            L"explorer.exe", L"ShellExperienceHost.exe", L"StartMenuExperienceHost.exe",
            L"SearchHost.exe", L"sihost.exe", L"fontdrvhost.exe", L"ChsIME.exe", L"ctfmon.exe",
            L"devenv.exe", L"csrss.exe", L"smss.exe", L"svchost.exe", L"WUDFRd.exe"};
        for (const auto process : upstream) {
            Check(Policy::IsBypassed(process), "every upstream bypass process is protected");
            std::wstring upper(process);
            for (auto& c : upper) if (c >= L'a' && c <= L'z') c = wchar_t(c - (L'a' - L'A'));
            Check(Policy::IsBypassed(L"C:\\Program Files\\\u4e2d\u6587\\" + upper),
                  "bypass matches case-insensitive Windows basename");
            Check(Policy::IsBypassed(L"C:/Apps/" + upper), "forward-slash path supported");
        }
        constexpr std::array companions{
            L"OpenEGoHubService.exe", L"OpenEGoHubServiceDebug.exe", L"OpenEGoHubTray.exe",
            L"OpenEGoHubSettings.exe", L"OpenEGoHubCleanup.exe", L"OpenEGoHubApp.exe",
            L"EGoTouchService.exe", L"EGoTouchDriver.exe", L"EGoTouchApp.exe", L"egotouchd.exe",
            L"EGoTouchRecorder.exe", L"GaokunThpHost.exe", L"GaokunPenHost.exe",
            L"GaokunKeyboardHost.exe", L"GaokunDisplay.exe", L"GaokunPower.exe", L"GaokunCtl.exe",
            L"BtMcuTestTool.exe", L"TsaRawReplay.exe", L"ChipHidProbeB.exe", L"HimaxThpProbe.exe",
            L"HuaweiThpService.exe", L"HuaweiPenEraserService.exe", L"HuaweiPenApp.exe",
            L"HW_OSDServer.exe", L"HWVEAudioService.exe", L"PenService.exe", L"KeyboardService.exe",
            L"THP_Service.exe", L"HiConnectivityService.exe", L"HwDistributedMainService.exe",
            L"HwPCCoreService.exe", L"LCD_Service.exe", L"MBAMainService.exe",
            L"MateBookService.exe", L"MouseCrossingDaemon.exe", L"WUCSProxy.exe",
            L"TextInputHost.exe", L"TabTip.exe", L"audiodg.exe"};
        for (const auto process : companions)
            Check(Policy::IsBypassed(process), "Hub, touch, HAL and vendor hosts must never be changed");
        Check(Policy::IsBypassed(L"c:\\random\\oPeNeGoFutureHost.EXE") &&
              Policy::IsBypassed(L"GaokunFuture.exe") && Policy::IsBypassed(L"HuaweiFuture.exe"),
              "future project and vendor companion prefixes conservatively protected");
        Check(Policy::IsBypassed(L"explorer") && Policy::IsBypassed(L"DEVENV"),
              "extensionless process names preserve protection");
        Check(Policy::IsBypassed(L"") && Policy::IsBypassed(L"C:\\Apps\\") &&
              Policy::IsBypassed(std::wstring_view(L"C:\\bad\0name\\tool.exe", 20)),
              "unqueryable or invalid image names fail closed");
        Check(!Policy::IsBypassed(L"ordinary.exe") && !Policy::IsBypassed(L"C:\\OpenEGoHub\\ordinary.exe") &&
              !Policy::IsBypassed(L"myegotouch.exe") && !Policy::IsBypassed(L"explorer_helper.exe"),
              "directory or substring matches do not exempt ordinary apps");

        constexpr std::array<uint32_t, 8> masks{
            0, 1, 2, 4, 0x80000000u, 0xfffffffeu, 0xffffffffu, 0x12345678u};
        for (auto originalControl : masks) for (auto originalState : masks) {
            const PowerState original{originalControl, originalState};
            const auto eco = Policy::EnableEco(original);
            Check((eco.controlMask & 1u) == 1 && (eco.stateMask & 1u) == 1 &&
                  (eco.controlMask & ~1u) == (originalControl & ~1u) &&
                  (eco.stateMask & ~1u) == (originalState & ~1u),
                  "EcoQoS sets execution-speed bits and preserves every unrelated flag");
            for (auto currentControl : masks) for (auto currentState : masks) {
                const auto restored = Policy::RestoreExecution({currentControl, currentState}, original);
                Check((restored.controlMask & 1u) == (originalControl & 1u) &&
                      (restored.stateMask & 1u) == (originalState & 1u) &&
                      (restored.controlMask & ~1u) == (currentControl & ~1u) &&
                      (restored.stateMask & ~1u) == (currentState & ~1u),
                      "restore changes only the execution-speed control and state bits");
            }
        }

        const std::vector<ProcessNode> tree{
            {1, 0, L"explorer.exe"}, {10, 1, L"C:\\Apps\\Browser.exe"},
            {11, 10, L"C:/Apps/BROWSER.EXE"}, {12, 10, L"renderer.exe"},
            {13, 11, L"helper.exe"}, {14, 13, L"nested.exe"}, {15, 1, L"other.exe"},
            {20, 1, L"launcher.exe"}, {30, 20, L"app.exe"}, {31, 30, L"app-worker.exe"},
            {32, 20, L"launcher-helper.exe"}};
        Family(tree, {11}, {10, 11, 12, 13, 14}, "foreground renderer protects same-image parent and its workers");
        Family(tree, {11, 11, 10, 13}, {10, 11, 12, 13, 14}, "repeated foreground roots and family members deduplicate");
        Family(tree, {13}, {13, 14}, "unrelated parent does not absorb its whole application tree");
        Family(tree, {30}, {30, 31}, "launcher and siblings stay outside a different-basename family");
        Family(tree, {11, 30}, {10, 11, 12, 13, 14, 30, 31}, "multiple foreground roots protect all actual children");
        Family(tree, {0}, {}, "PID zero cannot turn the entire session into a foreground family");
        Family({}, {91, 91, 0}, {91}, "explicit foreground PID stays protected during a snapshot race");
        Family({{92, 91, L"child.exe"}}, {91}, {91, 92}, "missing root snapshot still preserves known descendants");
        Family({{40, 0, L""}, {41, 40, L"app.exe"}}, {41}, {41}, "unknown ancestor cannot swallow a family");

        const auto desktop = Policy::ResolveForeground(1, L"C:\\Windows\\EXPLORER.EXE");
        Check(desktop.valid && !desktop.frame && Policy::IsBypassed(L"explorer.exe") &&
              Policy::ForegroundFamily(tree, desktop.roots).empty(),
              "known Explorer foreground never protects its entire session tree or loses its bypass");
        constexpr std::array ambient{
            L"explorer.exe", L"dwm.exe", L"sihost.exe", L"ShellExperienceHost.exe",
            L"StartMenuExperienceHost.exe", L"SearchHost.exe", L"winlogon.exe", L"csrss.exe", L"svchost.exe"};
        for (const auto shell : ambient) {
            const auto selected = Policy::ResolveForeground(1, shell);
            Check(selected.valid && Policy::IsBypassed(shell) &&
                  Policy::ForegroundFamily(tree, selected.roots).empty(),
                  "ambient hosts remain bypassed without exempting ordinary descendants");
        }
        const auto browser = Policy::ResolveForeground(11, L"C:/Apps/browser.EXE", std::array<uint32_t, 1>{300});
        const std::vector<uint32_t> browserFamily{10, 11, 12, 13, 14};
        Check(browser.valid && !browser.frame && Policy::ForegroundFamily(tree, browser.roots) == browserFamily,
              "ordinary foreground selection preserves the actual browser parent and helper family");
        const std::vector<ProcessNode> bypassedApps{
            {500, 1, L"msedge.exe"}, {501, 500, L"edge-helper.exe"},
            {600, 1, L"devenv.exe"}, {601, 600, L"build-helper.exe"}, {700, 1, L"other.exe"}};
        const auto edge = Policy::ResolveForeground(500, L"msedge.exe");
        const auto ide = Policy::ResolveForeground(600, L"C:\\VS\\DEVENV.EXE");
        Check(edge.valid && Policy::IsBypassed(L"msedge.exe") &&
              Policy::ForegroundFamily(bypassedApps, edge.roots) == std::vector<uint32_t>{500, 501} &&
              ide.valid && Policy::IsBypassed(L"devenv.exe") &&
              Policy::ForegroundFamily(bypassedApps, ide.roots) == std::vector<uint32_t>{600, 601},
              "ordinary bypassed applications still protect their helpers while remaining unwritable themselves");
        const auto unknown = Policy::ResolveForeground(11, L"");
        Check(!unknown.valid && !Policy::ResolveForeground(0, L"Browser.exe").valid &&
              !Policy::ResolveForeground(11, L"C:\\Apps\\").valid &&
              !Policy::ResolveForeground(11, std::wstring_view(L"bad\0name.exe", 12)).valid,
              "unknown or invalid foreground identity cannot be treated as an ambient shell");
        auto retainedFamily = browserFamily;
        if (unknown.valid) retainedFamily = Policy::ForegroundFamily(tree, unknown.roots);
        Check(retainedFamily == browserFamily,
              "invalid foreground resolution cannot clear prior protection and expose the active app");

        const std::vector<ProcessNode> uwp{
            {100, 1, L"ApplicationFrameHost.exe"}, {101, 100, L"view-helper.exe"},
            {200, 1, L"UwpApp.exe"}, {201, 200, L"worker.exe"}, {300, 1, L"unrelated.exe"}};
        Family(uwp, {100, 200, 200}, {100, 101, 200, 201}, "UWP host plus child-window roots remain distinct");
        for (auto pid : Policy::ForegroundFamily(uwp, std::array<uint32_t, 2>{100, 200}))
            if (pid == 100) Check(Policy::IsBypassed(uwp.front().image),
                                  "foreground roots do not grant permission to write bypassed hosts");
        const auto emptyFrame = Policy::ResolveForeground(100, L"ApplicationFrameHost.exe");
        const auto invalidChildren = Policy::ResolveForeground(100, L"ApplicationFrameHost.exe",
                                                               std::array<uint32_t, 3>{100, 0, 100});
        Check(!emptyFrame.valid && emptyFrame.frame && !invalidChildren.valid && invalidChildren.frame,
              "UWP host without a real child identity fails closed");
        const std::vector<ProcessNode> framedTree{
            {100, 1, L"ApplicationFrameHost.exe"}, {101, 100, L"other-host-child.exe"},
            {200, 100, L"UwpApp.exe"}, {201, 200, L"worker.exe"}, {300, 100, L"another-app.exe"}};
        const auto realFrame = Policy::ResolveForeground(100, L"C:/Windows/APPLICATIONFRAMEHOST.EXE",
            std::array<uint32_t, 7>{201, 100, 0, 200, 201, 200, 0});
        Check(realFrame.valid && realFrame.frame && realFrame.roots == std::vector<uint32_t>{200, 201} &&
              Policy::ForegroundFamily(framedTree, realFrame.roots) == std::vector<uint32_t>{200, 201},
              "UWP selection filters host and zero PIDs, deduplicates roots and protects only the actual app family");
        Family({{400, 0, L"EGoTouchDriver.exe"}, {401, 400, L"EGoTouchDriver.exe"},
                {402, 400, L"unrelated.exe"}}, {401}, {401},
               "bypassed same-name parent is never added by upward expansion");

        Family({{50, 51, L"same.exe"}, {51, 50, L"SAME.EXE"}, {52, 50, L"worker.exe"}},
               {50}, {50, 51, 52}, "cyclic ancestry and descendants terminate with unique PIDs");
        Family({{53, 53, L"self.exe"}}, {53, 53}, {53}, "self-parent cycle terminates");
        std::vector<ProcessNode> chain;
        for (uint32_t pid = 1; pid <= 4096; ++pid)
            chain.push_back({pid, pid - 1, L"worker.exe"});
        const auto largeFamily = Policy::ForegroundFamily(chain, std::array<uint32_t, 1>{4096});
        Check(largeFamily.size() == chain.size() && largeFamily.front() == 1 && largeFamily.back() == 4096,
              "deep process trees use bounded iteration rather than stack recursion");
        std::cout << "EnergyStar bypass, execution-bit preservation and foreground-family policy passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
