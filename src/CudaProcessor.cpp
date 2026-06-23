#include "CudaProcessor.h"

#if DAS_WITH_CUDA
bool ComputeGaugeProductsCudaImpl(const std::complex<float>* polX,
                                  const std::complex<float>* polY,
                                  std::size_t rangeCount,
                                  std::size_t pulseCount,
                                  std::size_t gaugeBins,
                                  std::vector<std::complex<float>>& gaugeProducts,
                                  std::string& errorMessage);
#endif

bool ComputeGaugeProductsCuda(const std::complex<float>* polX,
                              const std::complex<float>* polY,
                              std::size_t rangeCount,
                              std::size_t pulseCount,
                              std::size_t gaugeBins,
                              std::vector<std::complex<float>>& gaugeProducts,
                              std::string& errorMessage)
{
#if DAS_WITH_CUDA
    return ComputeGaugeProductsCudaImpl(polX, polY, rangeCount, pulseCount, gaugeBins, gaugeProducts, errorMessage);
#else
    (void)polX;
    (void)polY;
    (void)rangeCount;
    (void)pulseCount;
    (void)gaugeBins;
    (void)gaugeProducts;
    errorMessage = "CUDA was not enabled at build time.";
    return false;
#endif
}
