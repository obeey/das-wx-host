#pragma once

#include "DasTypes.h"

#include <random>

class DasSimulator {
public:
    AcquisitionFrame Generate(const AcquisitionConfig& config, double startTimeSec = 0.0);

private:
    std::mt19937 noiseRng_{11};
};
