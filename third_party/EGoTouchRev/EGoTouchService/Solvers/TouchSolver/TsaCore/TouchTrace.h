#pragma once
#include <cstdint>
namespace Solvers::Tsa::Core::TouchTrace {
enum class Stage : uint8_t { Raw, BeforeOutput, AfterOutput, Hid };
struct Event {
    Stage stage{}; uint8_t id=0; uint32_t event=0;
    uint32_t persist=0, flags=0, flags2=0, suppress=0, exception=0;
    int32_t grip=0; uint16_t age=0, major=0;
};
using Sink=void(*)(const Event&);
void SetSink(Sink sink);
void Table(Stage stage);
void Hid(uint8_t id,bool down);
}
