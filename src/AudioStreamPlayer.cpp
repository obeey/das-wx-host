#include "AudioStreamPlayer.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <numeric>
#include <thread>

namespace {
constexpr std::size_t kBufferCount = 5;
constexpr std::size_t kBufferFrames = 2048;
constexpr double kOutputGain = 0.85;

std::vector<std::int16_t> TraceToPcm(const std::vector<float>& trace, double traceSampleRateHz, int outputSampleRateHz)
{
    if (trace.size() < 2 || traceSampleRateHz <= 0.0 || outputSampleRateHz <= 0) {
        return {};
    }

    std::vector<float> centered(trace.size());
    const float mean = std::accumulate(trace.begin(), trace.end(), 0.0f) / static_cast<float>(trace.size());
    float maxAbs = 1.0e-6f;
    for (std::size_t i = 0; i < trace.size(); ++i) {
        centered[i] = trace[i] - mean;
        maxAbs = std::max(maxAbs, std::abs(centered[i]));
    }

    const double durationSec = static_cast<double>(trace.size() - 1) / traceSampleRateHz;
    const auto outputFrames = static_cast<std::size_t>(std::max(2.0, std::ceil(durationSec * outputSampleRateHz)));
    std::vector<std::int16_t> pcm(outputFrames);
    for (std::size_t i = 0; i < outputFrames; ++i) {
        const double sourceIndex = (static_cast<double>(i) / outputSampleRateHz) * traceSampleRateHz;
        const auto i0 = static_cast<std::size_t>(std::min(sourceIndex, static_cast<double>(centered.size() - 1)));
        const std::size_t i1 = std::min<std::size_t>(i0 + 1, centered.size() - 1);
        const double frac = sourceIndex - static_cast<double>(i0);
        const double sample = ((centered[i0] * (1.0 - frac) + centered[i1] * frac) / maxAbs) * kOutputGain;
        pcm[i] = static_cast<std::int16_t>(std::clamp(sample * 32767.0, -32767.0, 32767.0));
    }
    return pcm;
}
}

struct AudioStreamPlayer::Impl {
#ifdef _WIN32
    HWAVEOUT device = nullptr;
    std::vector<std::vector<std::int16_t>> buffers;
    std::vector<WAVEHDR> headers;
    std::vector<bool> freeBuffers;
#endif
    int sampleRateHz = 44100;
    std::deque<std::int16_t> queue;
    std::mutex mutex;
    std::condition_variable cv;
    std::thread worker;
    std::atomic_bool running = false;

#ifdef _WIN32
    static void CALLBACK WaveOutCallback(HWAVEOUT, UINT message, DWORD_PTR instance, DWORD_PTR param1, DWORD_PTR)
    {
        if (message != WOM_DONE || instance == 0 || param1 == 0) {
            return;
        }

        auto* self = reinterpret_cast<Impl*>(instance);
        auto* header = reinterpret_cast<WAVEHDR*>(param1);
        const auto index = static_cast<std::size_t>(header->dwUser);
        {
            std::lock_guard<std::mutex> lock(self->mutex);
            if (index < self->freeBuffers.size()) {
                self->freeBuffers[index] = true;
            }
        }
        self->cv.notify_one();
    }

    void WorkerLoop()
    {
        while (running) {
            std::size_t index = freeBuffers.size();
            {
                std::unique_lock<std::mutex> lock(mutex);
                cv.wait(lock, [&]() {
                    return !running || std::any_of(freeBuffers.begin(), freeBuffers.end(), [](bool free) { return free; });
                });
                if (!running) {
                    break;
                }
                for (std::size_t i = 0; i < freeBuffers.size(); ++i) {
                    if (freeBuffers[i]) {
                        freeBuffers[i] = false;
                        index = i;
                        break;
                    }
                }

                auto& buffer = buffers[index];
                for (std::int16_t& sample : buffer) {
                    if (queue.empty()) {
                        sample = 0;
                    } else {
                        sample = queue.front();
                        queue.pop_front();
                    }
                }
            }

            waveOutWrite(device, &headers[index], sizeof(WAVEHDR));
        }
    }
#endif
};

AudioStreamPlayer::~AudioStreamPlayer()
{
    Stop();
}

bool AudioStreamPlayer::Start(int outputSampleRateHz)
{
    Stop();
    impl_ = new Impl();
    impl_->sampleRateHz = outputSampleRateHz;

#ifdef _WIN32
    WAVEFORMATEX format = {};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 1;
    format.nSamplesPerSec = static_cast<DWORD>(outputSampleRateHz);
    format.wBitsPerSample = 16;
    format.nBlockAlign = format.nChannels * format.wBitsPerSample / 8;
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    const MMRESULT opened = waveOutOpen(
        &impl_->device,
        WAVE_MAPPER,
        &format,
        reinterpret_cast<DWORD_PTR>(&Impl::WaveOutCallback),
        reinterpret_cast<DWORD_PTR>(impl_),
        CALLBACK_FUNCTION);
    if (opened != MMSYSERR_NOERROR) {
        delete impl_;
        impl_ = nullptr;
        return false;
    }

    impl_->buffers.assign(kBufferCount, std::vector<std::int16_t>(kBufferFrames, 0));
    impl_->headers.assign(kBufferCount, WAVEHDR{});
    impl_->freeBuffers.assign(kBufferCount, true);
    for (std::size_t i = 0; i < kBufferCount; ++i) {
        impl_->headers[i].lpData = reinterpret_cast<LPSTR>(impl_->buffers[i].data());
        impl_->headers[i].dwBufferLength = static_cast<DWORD>(impl_->buffers[i].size() * sizeof(std::int16_t));
        impl_->headers[i].dwUser = static_cast<DWORD_PTR>(i);
        waveOutPrepareHeader(impl_->device, &impl_->headers[i], sizeof(WAVEHDR));
    }

    impl_->running = true;
    impl_->worker = std::thread(&Impl::WorkerLoop, impl_);
    impl_->cv.notify_one();
    return true;
#else
    impl_->running = true;
    return true;
#endif
}

void AudioStreamPlayer::Stop()
{
    if (!impl_) {
        return;
    }

    impl_->running = false;
    impl_->cv.notify_all();
    if (impl_->worker.joinable()) {
        impl_->worker.join();
    }

#ifdef _WIN32
    if (impl_->device) {
        waveOutReset(impl_->device);
        for (WAVEHDR& header : impl_->headers) {
            if ((header.dwFlags & WHDR_PREPARED) != 0) {
                waveOutUnprepareHeader(impl_->device, &header, sizeof(WAVEHDR));
            }
        }
        waveOutClose(impl_->device);
    }
#endif

    delete impl_;
    impl_ = nullptr;
}

void AudioStreamPlayer::Clear()
{
    if (!impl_) {
        return;
    }

    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->queue.clear();
}

bool AudioStreamPlayer::IsRunning() const
{
    return impl_ && impl_->running;
}

void AudioStreamPlayer::PushEventTrace(const std::vector<float>& trace, double traceSampleRateHz)
{
    if (!impl_ || !impl_->running) {
        return;
    }

    std::vector<std::int16_t> pcm = TraceToPcm(trace, traceSampleRateHz, impl_->sampleRateHz);
    if (pcm.empty()) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        const std::size_t maxQueuedSamples = static_cast<std::size_t>(impl_->sampleRateHz);
        while (impl_->queue.size() + pcm.size() > maxQueuedSamples && !impl_->queue.empty()) {
            impl_->queue.pop_front();
        }
        for (std::int16_t sample : pcm) {
            impl_->queue.push_back(sample);
        }
    }
    impl_->cv.notify_one();
}
