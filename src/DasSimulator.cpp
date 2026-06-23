#include "DasSimulator.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <random>

namespace {
constexpr double kPi = 3.1415926535897932384626433832795;

double SmoothGate(double t, double start, double duration, double ramp)
{
    if (t < start || t >= start + duration) {
        return 0.0;
    }
    if (ramp <= 0.0) {
        return 1.0;
    }
    if (t < start + ramp) {
        const double x = (t - start) / ramp;
        return 0.5 - 0.5 * std::cos(kPi * x);
    }
    if (t >= start + duration - ramp) {
        const double x = (start + duration - t) / ramp;
        return 0.5 - 0.5 * std::cos(kPi * x);
    }
    return 1.0;
}
}

AcquisitionFrame DasSimulator::Generate(const AcquisitionConfig& config) const
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
        frame.slowTimeSec[ip] = static_cast<float>(static_cast<double>(ip) / config.prfHz);
    }

    std::mt19937 rng(7);
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
            std::complex<float>(normal(rng), normal(rng));

        const float p = uniformPhase(rng);
        const std::complex<float> polRot(std::cos(p), std::sin(p));
        rayleighY[iz] = static_cast<float>(0.75 * loss / std::sqrt(2.0)) *
            std::complex<float>(normal(rng), normal(rng)) * polRot;

        const double rel = (z - config.eventPositionM) / std::max(0.5, config.eventWidthM);
        spatialProfile[iz] = std::exp(-0.5 * rel * rel);
        cumulative += spatialProfile[iz] * dz;
        cumulativeProfile[iz] = cumulative;
    }

    const double phaseScale = 4.0 * kPi * config.fiberIndex / config.wavelengthM;
    const double strainAmp = config.eventStrainNstrain * 1.0e-9;
    const double ramp = std::min(0.01, std::max(0.0, config.eventDurationSec * 0.5));
    const double noiseScale = std::pow(10.0, -config.snrDb / 20.0);

    for (std::size_t ip = 0; ip < np; ++ip) {
        const double t = frame.slowTimeSec[ip];
        const double gate = SmoothGate(t, config.eventStartSec, config.eventDurationSec, ramp);
        const double strainT = strainAmp * std::sin(2.0 * kPi * config.eventFrequencyHz * t) * gate;

        for (std::size_t iz = 0; iz < nz; ++iz) {
            const double phase = phaseScale * cumulativeProfile[iz] * strainT;
            const std::complex<float> ph(static_cast<float>(std::cos(phase)), static_cast<float>(std::sin(phase)));

            const float ampX = std::abs(rayleighX[iz]);
            const float ampY = std::abs(rayleighY[iz]);
            const float nx = static_cast<float>(noiseScale * std::max(ampX, 1.0e-4f) / std::sqrt(2.0));
            const float ny = static_cast<float>(noiseScale * std::max(ampY, 1.0e-4f) / std::sqrt(2.0));

            const std::size_t idx = ip * nz + iz;
            frame.polX[idx] = rayleighX[iz] * ph + nx * std::complex<float>(normal(rng), normal(rng));
            frame.polY[idx] = rayleighY[iz] * ph + ny * std::complex<float>(normal(rng), normal(rng));
            frame.bpd[idx] = ampX * ampX + ampY * ampY;
        }
    }

    return frame;
}
