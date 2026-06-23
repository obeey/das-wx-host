#include "NiDmaDecoder.h"

#include <algorithm>

int16_t NiDmaDecoder::SignExtend14(uint16_t word)
{
    const uint16_t raw = word & 0x3FFFu;
    const int16_t signedValue = (raw & 0x2000u) ? static_cast<int16_t>(raw | 0xC000u) : static_cast<int16_t>(raw);
    return signedValue;
}

NiDecodedFrame NiDmaDecoder::DecodeUnpackedInt16(const uint16_t* words, std::size_t wordCount, int channelCount)
{
    NiDecodedFrame frame;
    frame.channelCount = std::max(1, channelCount);
    frame.normalizedSamples.resize(wordCount);
    constexpr float scale = 1.0f / 8192.0f;
    for (std::size_t i = 0; i < wordCount; ++i) {
        frame.normalizedSamples[i] = static_cast<float>(SignExtend14(words[i])) * scale;
    }
    return frame;
}
