#include "DasSimulator.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <random>

namespace {
constexpr double kPi = 3.1415926535897932384626433832795;

double LoopSample(const std::vector<float>& samples, double sampleRateHz, double t)
{
    if (samples.empty() || sampleRateHz <= 0.0) {
        return 0.0;
    }

    const double sourcePosition = std::fmod(std::max(0.0, t) * sampleRateHz, static_cast<double>(samples.size()));
    const auto i0 = static_cast<std::size_t>(sourcePosition);
    const std::size_t i1 = (i0 + 1) % samples.size();
    const double frac = sourcePosition - static_cast<double>(i0);
    return static_cast<double>(samples[i0]) * (1.0 - frac) + static_cast<double>(samples[i1]) * frac;
}
}

AcquisitionFrame DasSimulator::Generate(const AcquisitionConfig& config, double startTimeSec)
{
    AcquisitionFrame frame;
    const double z0 = config.simulationStartM;
    const double z1 = std::max(config.simulationStartM + config.dzM, config.simulationStopM);
    const double dz = std::max(0.05, config.dzM);
    const auto nz = static_cast<std::size_t>(std::floor((z1 - z0) / dz)) + 1;
    const auto np = static_cast<std::size_t>(std::max(8, config.pulseCount));

    frame.rangeCount = nz;
    frame.pulseCount = np;
    frame.distanceM.resize(nz);
    frame.slowTimeSec.resize(np);
    frame.polX.resize(nz * np);
    frame.polY.resize(nz * np);
    frame.bpd.resize(nz * np);

    for (std::size_t iz = 0; iz < nz; ++iz) {
        frame.distanceM[iz] = static_cast<float>(z0 + static_cast<double>(iz) * dz);
    }
    for (std::size_t ip = 0; ip < np; ++ip) {
        frame.slowTimeSec[ip] = static_cast<float>(startTimeSec + static_cast<double>(ip) / config.prfHz);
    }

    std::mt19937 rayleighRng(7);
    std::normal_distribution<float> normal(0.0f, 1.0f);
    std::uniform_real_distribution<float> uniformPhase(0.0f, static_cast<float>(2.0 * kPi));

    std::vector<std::complex<float>> rayleighX(nz);
    std::vector<std::complex<float>> rayleighY(nz);
    std::vector<double> spatialProfile(nz);
    std::vector<double> cumulativeProfile(nz);

    const double attenuationDbPerKm = 0.20;
    const double attenuationNpPerM = attenuationDbPerKm / 1000.0 / 20.0 * std::log(10.0);

    double cumulative = 0.0;
    for (std::size_t iz = 0; iz < nz; ++iz) {
        const double z = frame.distanceM[iz];
        const double loss = std::exp(-attenuationNpPerM * z);
        rayleighX[iz] = static_cast<float>(loss / std::sqrt(2.0)) *
            std::complex<float>(normal(rayleighRng), normal(rayleighRng));

        const float p = uniformPhase(rayleighRng);
        const std::complex<float> polRot(std::cos(p), std::sin(p));
        rayleighY[iz] = static_cast<float>(0.75 * loss / std::sqrt(2.0)) *
            std::complex<float>(normal(rayleighRng), normal(rayleighRng)) * polRot;

        const double rel = (z - config.eventPositionM) / std::max(0.5, config.eventWidthM);
        spatialProfile[iz] = std::exp(-0.5 * rel * rel);
        cumulative += spatialProfile[iz] * dz;
        cumulativeProfile[iz] = cumulative;
    }

    const double phaseScale = 4.0 * kPi * config.fiberIndex / config.wavelengthM;
    const double strainAmp = config.eventStrainNstrain * 1.0e-9;
    const double noiseScale = std::pow(10.0, -config.snrDb / 20.0);

    for (std::size_t ip = 0; ip < np; ++ip) {
        const double t = frame.slowTimeSec[ip];
        const double vibration = config.useAudioVibration
                                     ? LoopSample(config.vibrationSamples, config.vibrationSampleRateHz, t)
                                     : std::sin(2.0 * kPi * config.eventFrequencyHz * t);
        const double strainT = strainAmp * vibration;

        for (std::size_t iz = 0; iz < nz; ++iz) {
            const double phase = phaseScale * cumulativeProfile[iz] * strainT;
            const std::complex<float> ph(static_cast<float>(std::cos(phase)), static_cast<float>(std::sin(phase)));

            const float ampX = std::abs(rayleighX[iz]);
            const float ampY = std::abs(rayleighY[iz]);
            const float nx = static_cast<float>(noiseScale * std::max(ampX, 1.0e-4f) / std::sqrt(2.0));
            const float ny = static_cast<float>(noiseScale * std::max(ampY, 1.0e-4f) / std::sqrt(2.0));

            const std::size_t idx = ip * nz + iz;
            frame.polX[idx] = rayleighX[iz] * ph + nx * std::complex<float>(normal(noiseRng_), normal(noiseRng_));
            frame.polY[idx] = rayleighY[iz] * ph + ny * std::complex<float>(normal(noiseRng_), normal(noiseRng_));
            frame.bpd[idx] = ampX * ampX + ampY * ampY;
        }
    }

    return frame;
}
