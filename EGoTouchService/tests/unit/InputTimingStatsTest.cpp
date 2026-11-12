#include "runtime/InputTimingStats.h"
#include <iostream>
#include <stdexcept>
static void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int main() {
    try {
        InputTimingStats s;
        s.Record(false, 3000000, 3000000, 20, 10);
        s.Record(true, 6000000, 3000000, 20, 10);
        s.Record(true, 6005000, 4700, 200, 100);
        Check(s.Read().frames == 1 && s.Read().maxPeriod == 5000 && s.Read().maxRead == 4700,
              "idle wake time must not inflate active drag timings");
        s.Record(true, 6035000, 20000, 7000, 3000);
        Check(s.Read().longPeriods == 1 && s.Read().maxPeriod == 30000 && s.Read().maxSolve == 7000 && s.Read().maxSend == 3000,
              "capture active stall and its phase");
        s.ResetWindow();
        s.Record(true, 6040000, 4700, 200, 100);
        Check(s.Read().periods == 1 && s.Read().maxPeriod == 5000,
              "heartbeat reset must preserve ongoing contact timing");
        s.Reset();
        s.Record(true, 10000000, 3000000, 20, 10);
        Check(s.Read().frames == 0 && s.Read().periods == 0, "restart resets continuity");
        std::cout << "Input timing idle/active/reset tests passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
