#pragma once

#include "DasTypes.h"

class DasDemodulator {
public:
    DasResult Process(const AcquisitionFrame& frame, const AcquisitionConfig& config) const;
    void UpdateEventSelection(DasResult& result, double selectedDistanceM, double selectedTimeSec) const;
};
