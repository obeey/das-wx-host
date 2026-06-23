#include "AudioFileDecoder.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <windows.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <string>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

namespace {
constexpr DWORD kFirstAudioStream = static_cast<DWORD>(static_cast<int>(MF_SOURCE_READER_FIRST_AUDIO_STREAM));

void ThrowIfFailed(HRESULT hr, const char* message)
{
    if (FAILED(hr)) {
        throw std::runtime_error(message);
    }
}

void EnsureMediaFoundation()
{
    static bool initialized = false;
    if (!initialized) {
        const HRESULT coInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(coInit) && coInit != RPC_E_CHANGED_MODE) {
            ThrowIfFailed(coInit, "COM initialization failed.");
        }
        ThrowIfFailed(MFStartup(MF_VERSION), "Media Foundation startup failed.");
        initialized = true;
    }
}
}

DecodedAudio DecodeAudioFile(const std::wstring& path)
{
    EnsureMediaFoundation();

    using Microsoft::WRL::ComPtr;

    ComPtr<IMFSourceReader> reader;
    ThrowIfFailed(MFCreateSourceReaderFromURL(path.c_str(), nullptr, &reader), "Could not open audio file.");

    ComPtr<IMFMediaType> requestedType;
    ThrowIfFailed(MFCreateMediaType(&requestedType), "Could not create audio media type.");
    ThrowIfFailed(requestedType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio), "Could not set audio major type.");
    ThrowIfFailed(requestedType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM), "Could not request PCM audio.");
    ThrowIfFailed(requestedType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16), "Could not request 16-bit PCM audio.");
    ThrowIfFailed(reader->SetCurrentMediaType(kFirstAudioStream, nullptr, requestedType.Get()),
                  "Could not configure audio decoder.");

    ComPtr<IMFMediaType> actualType;
    ThrowIfFailed(reader->GetCurrentMediaType(kFirstAudioStream, &actualType),
                  "Could not read audio format.");

    UINT32 sampleRate = 0;
    UINT32 channels = 0;
    ThrowIfFailed(actualType->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sampleRate), "Could not read audio sample rate.");
    ThrowIfFailed(actualType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels), "Could not read audio channel count.");
    if (sampleRate == 0 || channels == 0) {
        throw std::runtime_error("Audio file has an invalid format.");
    }

    DecodedAudio decoded;
    decoded.sampleRateHz = sampleRate;
    decoded.displayName = std::filesystem::path(path).filename().u8string();

    while (true) {
        DWORD streamIndex = 0;
        DWORD flags = 0;
        LONGLONG timestamp = 0;
        ComPtr<IMFSample> sample;
        ThrowIfFailed(reader->ReadSample(
                          kFirstAudioStream, 0, &streamIndex, &flags, &timestamp, &sample),
                      "Could not decode audio samples.");

        if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0) {
            break;
        }
        if (!sample) {
            continue;
        }

        ComPtr<IMFMediaBuffer> buffer;
        ThrowIfFailed(sample->ConvertToContiguousBuffer(&buffer), "Could not flatten audio buffer.");

        BYTE* raw = nullptr;
        DWORD maxLength = 0;
        DWORD currentLength = 0;
        ThrowIfFailed(buffer->Lock(&raw, &maxLength, &currentLength), "Could not lock audio buffer.");
        (void)maxLength;
        const auto* pcm = reinterpret_cast<const std::int16_t*>(raw);
        const std::size_t sampleFrames = currentLength / (sizeof(std::int16_t) * channels);
        decoded.samples.reserve(decoded.samples.size() + sampleFrames);
        for (std::size_t frame = 0; frame < sampleFrames; ++frame) {
            double mono = 0.0;
            for (UINT32 ch = 0; ch < channels; ++ch) {
                mono += static_cast<double>(pcm[frame * channels + ch]) / 32768.0;
            }
            decoded.samples.push_back(static_cast<float>(mono / static_cast<double>(channels)));
        }
        buffer->Unlock();
    }

    if (decoded.samples.empty()) {
        throw std::runtime_error("Audio file did not contain any decoded samples.");
    }

    const auto [minIt, maxIt] = std::minmax_element(decoded.samples.begin(), decoded.samples.end());
    const float dc = 0.5f * (*minIt + *maxIt);
    float maxAbs = 1.0e-6f;
    for (float& sample : decoded.samples) {
        sample -= dc;
        maxAbs = std::max(maxAbs, std::abs(sample));
    }
    for (float& sample : decoded.samples) {
        sample = std::clamp(sample / maxAbs, -1.0f, 1.0f);
    }

    return decoded;
}
#else
#include <stdexcept>

DecodedAudio DecodeAudioFile(const std::wstring&)
{
    throw std::runtime_error("Audio decoding is only implemented for Windows builds.");
}
#endif
