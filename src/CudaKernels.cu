#include "CudaProcessor.h"

#include <cuda_runtime.h>

#include <sstream>

namespace {
__device__ float2 cmul(float2 a, float2 b)
{
    return make_float2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x);
}

__device__ float2 cconj(float2 a)
{
    return make_float2(a.x, -a.y);
}

__global__ void GaugeProductKernel(const float2* polX,
                                   const float2* polY,
                                   std::size_t rangeCount,
                                   std::size_t pulseCount,
                                   std::size_t gaugeBins,
                                   float2* out)
{
    const std::size_t pairCount = rangeCount - gaugeBins;
    const std::size_t total = pairCount * pulseCount;
    const std::size_t linear = blockIdx.x * blockDim.x + threadIdx.x;
    if (linear >= total) {
        return;
    }

    const std::size_t ip = linear / pairCount;
    const std::size_t iz = linear - ip * pairCount;
    const std::size_t base = ip * rangeCount + iz;
    const std::size_t upper = base + gaugeBins;

    const float2 gx = cmul(polX[upper], cconj(polX[base]));
    const float2 gy = cmul(polY[upper], cconj(polY[base]));
    out[linear] = make_float2(gx.x + gy.x, gx.y + gy.y);
}

std::string CudaErrorText(cudaError_t err)
{
    std::ostringstream stream;
    stream << cudaGetErrorName(err) << ": " << cudaGetErrorString(err);
    return stream.str();
}
}

bool ComputeGaugeProductsCudaImpl(const std::complex<float>* polX,
                                  const std::complex<float>* polY,
                                  std::size_t rangeCount,
                                  std::size_t pulseCount,
                                  std::size_t gaugeBins,
                                  std::vector<std::complex<float>>& gaugeProducts,
                                  std::string& errorMessage)
{
    if (gaugeBins == 0 || gaugeBins >= rangeCount || pulseCount == 0) {
        errorMessage = "Invalid CUDA gauge product dimensions.";
        return false;
    }

    const std::size_t inputCount = rangeCount * pulseCount;
    const std::size_t pairCount = rangeCount - gaugeBins;
    const std::size_t outputCount = pairCount * pulseCount;
    gaugeProducts.resize(outputCount);

    float2* dPolX = nullptr;
    float2* dPolY = nullptr;
    float2* dOut = nullptr;

    const auto cleanup = [&]() {
        cudaFree(dPolX);
        cudaFree(dPolY);
        cudaFree(dOut);
    };

    cudaError_t err = cudaMalloc(&dPolX, inputCount * sizeof(float2));
    if (err != cudaSuccess) {
        errorMessage = CudaErrorText(err);
        cleanup();
        return false;
    }
    err = cudaMalloc(&dPolY, inputCount * sizeof(float2));
    if (err != cudaSuccess) {
        errorMessage = CudaErrorText(err);
        cleanup();
        return false;
    }
    err = cudaMalloc(&dOut, outputCount * sizeof(float2));
    if (err != cudaSuccess) {
        errorMessage = CudaErrorText(err);
        cleanup();
        return false;
    }

    err = cudaMemcpy(dPolX, polX, inputCount * sizeof(float2), cudaMemcpyHostToDevice);
    if (err == cudaSuccess) {
        err = cudaMemcpy(dPolY, polY, inputCount * sizeof(float2), cudaMemcpyHostToDevice);
    }
    if (err != cudaSuccess) {
        errorMessage = CudaErrorText(err);
        cleanup();
        return false;
    }

    const int threads = 256;
    const int blocks = static_cast<int>((outputCount + threads - 1) / threads);
    GaugeProductKernel<<<blocks, threads>>>(dPolX, dPolY, rangeCount, pulseCount, gaugeBins, dOut);
    err = cudaGetLastError();
    if (err == cudaSuccess) {
        err = cudaDeviceSynchronize();
    }
    if (err != cudaSuccess) {
        errorMessage = CudaErrorText(err);
        cleanup();
        return false;
    }

    err = cudaMemcpy(gaugeProducts.data(), dOut, outputCount * sizeof(float2), cudaMemcpyDeviceToHost);
    if (err != cudaSuccess) {
        errorMessage = CudaErrorText(err);
        cleanup();
        return false;
    }

    cleanup();
    return true;
}
