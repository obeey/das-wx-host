#pragma once

#include <vector>

class AudioStreamPlayer {
public:
    AudioStreamPlayer() = default;
    ~AudioStreamPlayer();

    bool Start(int outputSampleRateHz = 44100);
    void Stop();
    void Clear();
    bool IsRunning() const;
    void PushEventTrace(const std::vector<float>& trace, double traceSampleRateHz);

private:
    struct Impl;
    Impl* impl_ = nullptr;
};
