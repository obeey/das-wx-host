#include "DasDemodulator.h"

#include "CudaProcessor.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numeric>
#include <sstream>

namespace {
constexpr double kPi = 3.1415926535897932384626433832795;
constexpr double kLightSpeed = 299792458.0;

float WrapDelta(float delta)
{
    while (delta > static_cast<float>(kPi)) {
        delta -= static_cast<float>(2.0 * kPi);
    }
    while (delta < static_cast<float>(-kPi)) {
        delta += static_cast<float>(2.0 * kPi);
    }
    return delta;
}

std::complex<float> GaugeProductCpu(const std::complex<float>& upperX,
                                    const std::complex<float>& lowerX,
                                    const std::complex<float>& upperY,
                                    const std::complex<float>& lowerY)
{
    return upperX * std::conj(lowerX) + upperY * std::conj(lowerY);
}

std::vector<std::complex<float>> BuildGaugeProductsCpu(const AcquisitionFrame& frame, std::size_t gaugeBins)
{
    const std::size_t pairCount = frame.rangeCount - gaugeBins;
    std::vector<std::complex<float>> products(pairCount * frame.pulseCount);
    for (std::size_t ip = 0; ip < frame.pulseCount; ++ip) {
        for (std::size_t iz = 0; iz < pairCount; ++iz) {
            const std::size_t lower = ip * frame.rangeCount + iz;
            const std::size_t upper = lower + gaugeBins;
            products[ip * pairCount + iz] = GaugeProductCpu(
                frame.polX[upper], frame.polX[lower], frame.polY[upper], frame.polY[lower]);
        }
    }
    return products;
}

void RemoveMovingAverage(std::vector<float>& data, std::size_t rows, std::size_t cols, std::size_t window)
{
    if (window < 3) {
        return;
    }
    window = std::min(window, cols);
    const std::size_t half = window / 2;
    std::vector<float> temp(cols);

    for (std::size_t row = 0; row < rows; ++row) {
        float sum = 0.0f;
        std::size_t left = 0;
        std::size_t right = 0;
        for (std::size_t col = 0; col < cols; ++col) {
            while (right < cols && right <= col + half) {
                sum += data[row * cols + right];
                ++right;
            }
            while (left < col - std::min(col, half)) {
                sum -= data[row * cols + left];
                ++left;
            }
            temp[col] = data[row * cols + col] - sum / static_cast<float>(right - left);
        }
        std::copy(temp.begin(), temp.end(), data.begin() + static_cast<std::ptrdiff_t>(row * cols));
    }
}

std::size_t NearestIndex(const std::vector<float>& values, double target)
{
    if (values.empty()) {
        return 0;
    }
    std::size_t best = 0;
    double bestDistance = std::abs(static_cast<double>(values[0]) - target);
    for (std::size_t i = 1; i < values.size(); ++i) {
        const double d = std::abs(static_cast<double>(values[i]) - target);
        if (d < bestDistance) {
            best = i;
            bestDistance = d;
        }
    }
    return best;
}
}

DasResult DasDemodulator::Process(const AcquisitionFrame& frame, const AcquisitionConfig& config) const
{
    DasResult result;
    result.config = config;
    result.slowTimeSec = frame.slowTimeSec;
    result.pulseCount = frame.pulseCount;
    result.rangeResolutionM = kLightSpeed / (2.0 * config.fiberIndex * config.chirpBandwidthHz);

    const std::size_t gaugeBins = std::max<std::size_t>(1, static_cast<std::size_t>(std::llround(config.gaugeLengthM / config.dzM)));
    if (frame.rangeCount <= gaugeBins + 1 || frame.pulseCount < 8) {
        result.status = "Not enough samples for the selected gauge length.";
        return result;
    }

    const std::size_t pairCount = frame.rangeCount - gaugeBins;
    result.rangeCount = pairCount;
    result.distanceM.resize(pairCount);
    for (std::size_t iz = 0; iz < pairCount; ++iz) {
        result.distanceM[iz] = 0.5f * (frame.distanceM[iz] + frame.distanceM[iz + gaugeBins]);
    }

    result.rayleighDb.resize(frame.rangeCount);
    for (std::size_t iz = 0; iz < frame.rangeCount; ++iz) {
        const auto v = frame.polX[iz] + frame.polY[iz];
        result.rayleighDb[iz] = 20.0f * std::log10(std::abs(v) + 1.0e-9f);
    }

    std::vector<std::complex<float>> products;
    std::string cudaError;
    if (config.useCuda) {
        result.cudaUsed = ComputeGaugeProductsCuda(
            frame.polX.data(), frame.polY.data(), frame.rangeCount, frame.pulseCount, gaugeBins, products, cudaError);
    }
    if (!result.cudaUsed) {
        products = BuildGaugeProductsCpu(frame, gaugeBins);
    }

    result.dynamicStrainNstrain.assign(pairCount * frame.pulseCount, 0.0f);
    const double strainScale = config.wavelengthM / (4.0 * kPi * config.fiberIndex * config.gaugeLengthM) * 1.0e9;

    for (std::size_t iz = 0; iz < pairCount; ++iz) {
        const std::complex<float> baseline = std::conj(products[iz]);
        float unwrapped = 0.0f;
        float previous = std::arg(products[iz] * baseline);
        result.dynamicStrainNstrain[iz * frame.pulseCount] = static_cast<float>(previous * strainScale);

        for (std::size_t ip = 1; ip < frame.pulseCount; ++ip) {
            const float phase = std::arg(products[ip * pairCount + iz] * baseline);
            unwrapped += WrapDelta(phase - previous);
            previous = phase;
            result.dynamicStrainNstrain[iz * frame.pulseCount + ip] = static_cast<float>(unwrapped * strainScale);
        }
    }

    const std::size_t driftWindow = std::max<std::size_t>(5, static_cast<std::size_t>(config.prfHz / 40.0));
    RemoveMovingAverage(result.dynamicStrainNstrain, pairCount, frame.pulseCount, driftWindow | 1U);

    const double defaultTimeSec = !frame.slowTimeSec.empty()
                                      ? 0.5 * (static_cast<double>(frame.slowTimeSec.front()) + static_cast<double>(frame.slowTimeSec.back()))
                                      : 0.0;
    UpdateEventSelection(result, config.eventPositionM, defaultTimeSec);

    std::ostringstream status;
    status << "Range resolution " << result.rangeResolutionM << " m, selected "
           << result.selectedDistanceM << " m, estimated " << result.estimatedFrequencyHz << " Hz";
    if (result.cudaUsed) {
        status << ", CUDA gauge products";
    } else if (!cudaError.empty()) {
        status << ", CPU fallback (" << cudaError << ")";
    } else {
        status << ", CPU demodulation";
    }
    result.status = status.str();
    return result;
}

void DasDemodulator::UpdateEventSelection(DasResult& result, double selectedDistanceM, double selectedTimeSec) const
{
    if (result.rangeCount == 0 || result.pulseCount == 0 || result.distanceM.empty() || result.dynamicStrainNstrain.empty()) {
        return;
    }

    const std::size_t eventIndex = NearestIndex(result.distanceM, selectedDistanceM);
    result.selectedDistanceM = result.distanceM[eventIndex];
    result.eventTimeSec = result.slowTimeSec;
    result.eventTraceNstrain.resize(result.pulseCount);
    for (std::size_t ip = 0; ip < result.pulseCount; ++ip) {
        result.eventTraceNstrain[ip] = result.dynamicStrainNstrain[eventIndex * result.pulseCount + ip];
    }

    if (result.slowTimeSec.empty()) {
        result.selectedTimeSec = 0.0;
    } else {
        result.selectedTimeSec = std::clamp(
            selectedTimeSec,
            static_cast<double>(result.slowTimeSec.front()),
            static_cast<double>(result.slowTimeSec.back()));
    }

    const std::size_t nfft = result.pulseCount;
    const std::size_t bins = nfft / 2 + 1;
    result.spectrumHz.assign(bins, 0.0f);
    result.spectrumDb.assign(bins, 0.0f);
    if (result.eventTraceNstrain.size() < 2) {
        result.estimatedFrequencyHz = 0.0;
        return;
    }

    std::vector<float> windowed(result.pulseCount);
    const float mean = std::accumulate(result.eventTraceNstrain.begin(), result.eventTraceNstrain.end(), 0.0f) /
        static_cast<float>(std::max<std::size_t>(1, result.eventTraceNstrain.size()));
    for (std::size_t i = 0; i < result.pulseCount; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) / static_cast<double>(result.pulseCount - 1));
        windowed[i] = static_cast<float>((result.eventTraceNstrain[i] - mean) * w);
    }

    float maxMag = 1.0e-12f;
    std::vector<float> mags(bins);
    for (std::size_t k = 0; k < bins; ++k) {
        double re = 0.0;
        double im = 0.0;
        for (std::size_t n = 0; n < result.pulseCount; ++n) {
            const double a = -2.0 * kPi * static_cast<double>(k) * static_cast<double>(n) / static_cast<double>(nfft);
            re += windowed[n] * std::cos(a);
            im += windowed[n] * std::sin(a);
        }
        mags[k] = static_cast<float>(std::sqrt(re * re + im * im));
        maxMag = std::max(maxMag, mags[k]);
        result.spectrumHz[k] = static_cast<float>(static_cast<double>(k) * result.config.prfHz / static_cast<double>(nfft));
    }

    std::size_t peakIndex = bins > 1 ? 1 : 0;
    for (std::size_t k = peakIndex + 1; k < bins; ++k) {
        if (mags[k] > mags[peakIndex]) {
            peakIndex = k;
        }
    }
    result.estimatedFrequencyHz = result.spectrumHz[peakIndex];

    for (std::size_t k = 0; k < bins; ++k) {
        result.spectrumDb[k] = 20.0f * std::log10(mags[k] / maxMag + 1.0e-9f);
    }
}
