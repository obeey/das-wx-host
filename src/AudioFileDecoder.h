#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct DecodedAudio {
    std::vector<float> samples;
    std::uint32_t sampleRateHz = 0;
    std::string displayName;
};

DecodedAudio DecodeAudioFile(const std::wstring& path);
