#include "TsaContactEvents.h"
#include <iostream>
int main() {
    using namespace Solvers::Tsa::ContactEvents;
    if (Down(0) || Down(1) || Lift(1) ||
        !Down(2) || !Down(4) || !Down(8) || !Down(16) ||
        Down(32) || !Lift(32) || Down(64) || !Lift(64)) {
        std::cerr << "Raw TSA lifecycle normalization failed\n"; return 1;
    }
    std::cout << "Down/move/pending lift/final lift contact contract passed\n";
    return 0;
}
