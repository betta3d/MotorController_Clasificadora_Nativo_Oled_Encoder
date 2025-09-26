#pragma once
#include <Arduino.h>
#include <cstdint>

namespace App {

enum class HomingPhase : uint8_t {
    SEEK = 0,
    OFFSET = 1,
    STABILIZE = 2,
    DONE = 3,
    FAULT = 4
};

struct HomingContext {
    HomingPhase phase;
    int64_t baselineSteps;
    bool sensorFound;
    uint32_t stabilizeStartMs;
};

extern HomingContext homingCtx;

void startCentralizedHoming();
void processCentralizedHoming();
bool centralizedHomingCompleted();

} // namespace App