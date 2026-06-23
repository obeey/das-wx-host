#pragma once

#include <cstdint>
#include <vector>

struct NiDecodedFrame {
    int channelCount = 0;
    std::vector<float> normalizedSamples;
};

class NiDmaDecoder {
public:
    static int16_t SignExtend14(uint16_t word);
    static NiDecodedFrame DecodeUnpackedInt16(const uint16_t* words, std::size_t wordCount, int channelCount);
};
