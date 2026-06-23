#pragma once

#include "DasTypes.h"

class DasDemodulator {
public:
    DasResult Process(const AcquisitionFrame& frame, const AcquisitionConfig& config) const;
};
