#pragma once

#include "DasTypes.h"

class DasSimulator {
public:
    AcquisitionFrame Generate(const AcquisitionConfig& config) const;
};
