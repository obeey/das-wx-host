#pragma once

#include <complex>
#include <cstddef>
#include <string>
#include <vector>

struct AcquisitionConfig {
    double adcSampleRateHz = 1.3e9;
    int adcBits = 14;
    int adcChannels = 4;
    double pciExpressLanes = 8.0;

    double chirpBandwidthHz = 300e6;
    double chirpDurationSec = 1.0e-6;
    double aomStartFrequencyHz = 80e6;
    double opticalPulseWidthSec = 100e-9;

    double fiberLengthM = 40000.0;
    double simulationStartM = 0.0;
    double simulationStopM = 5000.0;
    double dzM = 1.0;
    double gaugeLengthM = 10.0;
    double prfHz = 2000.0;
    int pulseCount = 512;

    double wavelengthM = 1550.12e-9;
    double fiberIndex = 1.4682;
    double snrDb = 24.0;

    double eventPositionM = 1200.0;
    double eventWidthM = 8.0;
    double eventFrequencyHz = 100.0;
    double eventStartSec = 0.08;
    double eventDurationSec = 0.25;
    double eventStrainNstrain = 80.0;

    bool useCuda = true;
};

struct AcquisitionFrame {
    std::size_t rangeCount = 0;
    std::size_t pulseCount = 0;
    std::vector<float> distanceM;
    std::vector<float> slowTimeSec;
    std::vector<std::complex<float>> polX;
    std::vector<std::complex<float>> polY;
    std::vector<float> bpd;
};

struct DasResult {
    AcquisitionConfig config;
    std::vector<float> distanceM;
    std::vector<float> slowTimeSec;
    std::vector<float> rayleighDb;
    std::vector<float> dynamicStrainNstrain;
    std::size_t rangeCount = 0;
    std::size_t pulseCount = 0;

    std::vector<float> eventTimeSec;
    std::vector<float> eventTraceNstrain;
    std::vector<float> spectrumHz;
    std::vector<float> spectrumDb;

    double rangeResolutionM = 0.0;
    double estimatedFrequencyHz = 0.0;
    double selectedDistanceM = 0.0;
    bool cudaUsed = false;
    std::string status;
};
