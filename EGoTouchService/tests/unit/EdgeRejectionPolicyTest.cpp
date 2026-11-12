#include "EdgeRejectionPolicy.h"
#include <iostream>
int main() {
    using namespace Solvers::Tsa::EdgeRejectionPolicy;
    if (!OutsideSmallFingerBand(12, 0x101, 0x81, false, false) ||
        !OutsideSmallFingerBand(12, 0x101, 0x101, true, false) ||
        !OutsideSmallFingerBand(12, 0x181, 0x81, false, true) ||
        OutsideSmallFingerBand(12, 0x100, 0x81, false, false) ||
        OutsideSmallFingerBand(12, 0x101, 0x80, false, false) ||
        OutsideSmallFingerBand(12, 0x100, 0x100, true, false) ||
        OutsideSmallFingerBand(12, 0x180, 0x81, false, true) ||
        OutsideSmallFingerBand(20, 0x300, 0x300, false, false)) {
        std::cerr << "Narrow edge band boundaries or palm exclusion failed\n"; return 1;
    }
    std::cout << "Half-width finger bands and unchanged large-touch exclusion passed\n";
    return 0;
}
