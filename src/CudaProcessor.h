#pragma once

#include <complex>
#include <cstddef>
#include <string>
#include <vector>

bool ComputeGaugeProductsCuda(const std::complex<float>* polX,
                              const std::complex<float>* polY,
                              std::size_t rangeCount,
                              std::size_t pulseCount,
                              std::size_t gaugeBins,
                              std::vector<std::complex<float>>& gaugeProducts,
                              std::string& errorMessage);
