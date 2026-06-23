#include "MainFrame.h"

#include "AudioFileDecoder.h"
#include "AudioStreamPlayer.h"
#include "DasDemodulator.h"
#include "DasSimulator.h"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/event.h>
#include <wx/filedlg.h>
#include <wx/icon.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <sstream>
#include <thread>

namespace {
constexpr int kRunButtonId = wxID_HIGHEST + 11;
constexpr int kChooseAudioButtonId = wxID_HIGHEST + 12;
constexpr int kPlayEventAudioId = wxID_HIGHEST + 13;
constexpr int kEventAudioSampleRateHz = 44100;
constexpr double kPi = 3.1415926535897932384626433832795;

wxString UiText(const char* text)
{
    return wxString::FromUTF8(text);
}

std::vector<float> ResampleForSlowTime(const std::vector<float>& source, double sourceRateHz, double targetRateHz)
{
    if (source.empty() || sourceRateHz <= 0.0 || targetRateHz <= 0.0) {
        return {};
    }

    const double durationSec = static_cast<double>(source.size()) / sourceRateHz;
    const auto targetCount = static_cast<std::size_t>(std::max(1.0, std::ceil(durationSec * targetRateHz)));
    std::vector<float> target(targetCount);

    for (std::size_t i = 0; i < targetCount; ++i) {
        if (targetRateHz >= sourceRateHz) {
            const double sourceIndex = static_cast<double>(i) * sourceRateHz / targetRateHz;
            const auto i0 = static_cast<std::size_t>(std::min<double>(sourceIndex, source.size() - 1));
            const std::size_t i1 = std::min<std::size_t>(i0 + 1, source.size() - 1);
            const double frac = sourceIndex - static_cast<double>(i0);
            target[i] = static_cast<float>(source[i0] * (1.0 - frac) + source[i1] * frac);
            continue;
        }

        const double begin = static_cast<double>(i) * sourceRateHz / targetRateHz;
        const double end = static_cast<double>(i + 1) * sourceRateHz / targetRateHz;
        const auto first = static_cast<std::size_t>(std::floor(begin));
        const auto last = static_cast<std::size_t>(std::min<double>(source.size(), std::ceil(end)));

        double sum = 0.0;
        double weightSum = 0.0;
        for (std::size_t j = first; j < last; ++j) {
            const double left = std::max(begin, static_cast<double>(j));
            const double right = std::min(end, static_cast<double>(j + 1));
            const double weight = std::max(0.0, right - left);
            sum += static_cast<double>(source[j]) * weight;
            weightSum += weight;
        }
        target[i] = static_cast<float>(weightSum > 0.0 ? sum / weightSum : source[std::min(first, source.size() - 1)]);
    }

    float maxAbs = 1.0e-6f;
    for (float sample : target) {
        maxAbs = std::max(maxAbs, std::abs(sample));
    }
    for (float& sample : target) {
        sample = std::clamp(sample / maxAbs, -1.0f, 1.0f);
    }

    return target;
}

std::vector<float> ApplyAcousticFiberCoupling(const std::vector<float>& acousticPressure, double sampleRateHz)
{
    if (acousticPressure.empty() || sampleRateHz <= 0.0) {
        return {};
    }

    std::vector<float> coupled(acousticPressure.size());

    // Approximate speaker-to-fiber transfer: remove static pressure, emphasize
    // mid-band vibration, and smooth the strain response as a mechanical system.
    const double highPassCutoffHz = 20.0;
    const double lowPassCutoffHz = std::min(0.45 * sampleRateHz, 3500.0);
    const double hpRc = 1.0 / (2.0 * kPi * highPassCutoffHz);
    const double lpRc = 1.0 / (2.0 * kPi * std::max(10.0, lowPassCutoffHz));
    const double dt = 1.0 / sampleRateHz;
    const double hpAlpha = hpRc / (hpRc + dt);
    const double lpAlpha = dt / (lpRc + dt);

    double previousInput = acousticPressure.front();
    double highPassed = 0.0;
    double lowPassed = 0.0;
    for (std::size_t i = 0; i < acousticPressure.size(); ++i) {
        const double input = acousticPressure[i];
        highPassed = hpAlpha * (highPassed + input - previousInput);
        previousInput = input;
        lowPassed += lpAlpha * (highPassed - lowPassed);
        coupled[i] = static_cast<float>(lowPassed);
    }

    float maxAbs = 1.0e-6f;
    for (float sample : coupled) {
        maxAbs = std::max(maxAbs, std::abs(sample));
    }
    for (float& sample : coupled) {
        sample = std::clamp(sample / maxAbs, -1.0f, 1.0f);
    }

    return coupled;
}
}

MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, UiText("DAS 上位机 - 仿真版"), wxDefaultPosition, wxSize(1380, 860))
{
#ifdef __WXMSW__
    SetIcon(wxICON(APP_ICON));
#endif
    BuildUi();
    Bind(wxEVT_SIZE, &MainFrame::OnSize, this);
    Bind(wxEVT_BUTTON, &MainFrame::OnRun, this, kRunButtonId);
    Bind(wxEVT_BUTTON, &MainFrame::OnChooseAudio, this, kChooseAudioButtonId);
    Bind(wxEVT_CHECKBOX, &MainFrame::OnPlayEventAudio, this, kPlayEventAudioId);
    Bind(wxEVT_CLOSE_WINDOW, &MainFrame::OnClose, this);
}

MainFrame::~MainFrame()
{
    stopRequested_ = true;
    StopEventAudio();
    if (worker_.joinable()) {
        worker_.join();
    }
}

wxSpinCtrlDouble* MainFrame::AddSpin(wxWindow* parent,
                                     wxSizer* sizer,
                                     const wxString& label,
                                     double value,
                                     double minValue,
                                     double maxValue,
                                     double increment,
                                     int digits)
{
    auto* row = new wxBoxSizer(wxHORIZONTAL);
    auto* text = new wxStaticText(parent, wxID_ANY, label, wxDefaultPosition, wxSize(118, -1));
    auto* spin = new wxSpinCtrlDouble(parent, wxID_ANY);
    spin->SetRange(minValue, maxValue);
    spin->SetIncrement(increment);
    spin->SetDigits(digits);
    spin->SetValue(value);
    row->Add(text, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    row->Add(spin, 1, wxEXPAND);
    sizer->Add(row, 0, wxEXPAND | wxBOTTOM, 7);
    return spin;
}

void MainFrame::BuildUi()
{
    auto* root = new wxBoxSizer(wxHORIZONTAL);

    auto* leftPanel = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxSize(315, -1));
    leftPanel->SetScrollRate(0, 12);
    auto* left = new wxBoxSizer(wxVERTICAL);

    auto* title = new wxStaticText(leftPanel, wxID_ANY, UiText("DAS 参数配置"));
    title->SetFont(wxFontInfo(12).Bold());
    left->Add(title, 0, wxEXPAND | wxALL, 10);

    chirpBandwidthMHz_ = AddSpin(leftPanel, left, UiText("DA 扫频带宽 MHz"), 300.0, 10.0, 800.0, 10.0, 1);
    chirpDurationUs_ = AddSpin(leftPanel, left, UiText("啁啾时宽 us"), 1.0, 0.05, 50.0, 0.05, 3);
    aomStartMHz_ = AddSpin(leftPanel, left, UiText("AOM 起始 MHz"), 80.0, 10.0, 400.0, 1.0, 1);
    pulseWidthNs_ = AddSpin(leftPanel, left, UiText("光开关脉宽 ns"), 100.0, 20.0, 1000.0, 10.0, 1);
    adcGsps_ = AddSpin(leftPanel, left, UiText("AD 采样 Gsps"), 1.3, 0.1, 5.0, 0.1, 3);
    fiberKm_ = AddSpin(leftPanel, left, UiText("模块距离 km"), 40.0, 1.0, 100.0, 1.0, 1);
    simStopM_ = AddSpin(leftPanel, left, UiText("演示窗口 m"), 5000.0, 100.0, 80000.0, 100.0, 0);
    dzM_ = AddSpin(leftPanel, left, UiText("距离间隔 m"), 1.0, 0.1, 10.0, 0.1, 2);
    gaugeM_ = AddSpin(leftPanel, left, "Gauge m", 10.0, 1.0, 100.0, 1.0, 1);
    prfHz_ = AddSpin(leftPanel, left, "PRF Hz", 2000.0, 10.0, 20000.0, 10.0, 0);
    pulseCount_ = AddSpin(leftPanel, left, UiText("脉冲数"), 512.0, 64.0, 4096.0, 64.0, 0);
    snrDb_ = AddSpin(leftPanel, left, "SNR dB", 24.0, 0.0, 60.0, 1.0, 1);
    eventPositionM_ = AddSpin(leftPanel, left, UiText("事件位置 m"), 1200.0, 0.0, 80000.0, 50.0, 0);
    eventFrequencyHz_ = AddSpin(leftPanel, left, UiText("事件频率 Hz"), 100.0, 1.0, 5000.0, 1.0, 1);
    eventStrainNstrain_ = AddSpin(leftPanel, left, UiText("事件强度 nstrain"), 80.0, 1.0, 1000.0, 5.0, 1);

    useCuda_ = new wxCheckBox(leftPanel, wxID_ANY, UiText("尽量使用 CUDA"));
    useCuda_->SetValue(true);
    left->Add(useCuda_, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

    chooseAudioButton_ = new wxButton(leftPanel, kChooseAudioButtonId, UiText("选择外部声源音频"));
    left->Add(chooseAudioButton_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    audioSourceText_ = new wxStaticText(leftPanel, wxID_ANY, UiText("音频源：未选择"), wxDefaultPosition, wxSize(280, -1), wxST_NO_AUTORESIZE);
    left->Add(audioSourceText_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    playEventAudio_ = new wxCheckBox(leftPanel, kPlayEventAudioId, UiText("实时播放事件点解调音频"));
    left->Add(playEventAudio_, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

    runButton_ = new wxButton(leftPanel, kRunButtonId, UiText("开始连续模拟"));
    left->Add(runButton_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

    statusText_ = new wxStaticText(leftPanel, wxID_ANY, UiText("当前使用模拟数据，等待运行。"), wxDefaultPosition, wxSize(280, -1), wxST_NO_AUTORESIZE);
    left->Add(statusText_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
    leftPanel->SetSizer(left);
    root->Add(leftPanel, 0, wxEXPAND);

    auto* plots = new wxGridSizer(2, 2, 8, 8);
    rayleighPlot_ = new PlotPanel(this, PlotKind::RayleighTrace, UiText("压缩瑞利迹线"));
    waterfallPlot_ = new PlotPanel(this, PlotKind::Waterfall, UiText("动态应变瀑布图"));
    tracePlot_ = new PlotPanel(this, PlotKind::EventTrace, UiText("事件点时域"));
    spectrumPlot_ = new PlotPanel(this, PlotKind::Spectrum, UiText("事件点频谱"));
    waterfallPlot_->SetWaterfallSelectionHandler([this](double timeSec, double distanceM) {
        OnWaterfallSelected(timeSec, distanceM);
    });
    plots->Add(rayleighPlot_, 1, wxEXPAND);
    plots->Add(waterfallPlot_, 1, wxEXPAND);
    plots->Add(tracePlot_, 1, wxEXPAND);
    plots->Add(spectrumPlot_, 1, wxEXPAND);
    root->Add(plots, 1, wxEXPAND | wxALL, 8);

    SetSizer(root);
    Centre();
}

AcquisitionConfig MainFrame::ReadConfig() const
{
    AcquisitionConfig config;
    config.chirpBandwidthHz = chirpBandwidthMHz_->GetValue() * 1.0e6;
    config.chirpDurationSec = chirpDurationUs_->GetValue() * 1.0e-6;
    config.aomStartFrequencyHz = aomStartMHz_->GetValue() * 1.0e6;
    config.opticalPulseWidthSec = pulseWidthNs_->GetValue() * 1.0e-9;
    config.adcSampleRateHz = adcGsps_->GetValue() * 1.0e9;
    config.fiberLengthM = fiberKm_->GetValue() * 1000.0;
    config.simulationStartM = 0.0;
    config.simulationStopM = simStopM_->GetValue();
    config.dzM = dzM_->GetValue();
    config.gaugeLengthM = gaugeM_->GetValue();
    config.prfHz = prfHz_->GetValue();
    config.pulseCount = static_cast<int>(pulseCount_->GetValue());
    config.snrDb = snrDb_->GetValue();
    config.eventPositionM = std::min(eventPositionM_->GetValue(), config.simulationStopM);
    config.eventFrequencyHz = eventFrequencyHz_->GetValue();
    config.eventStrainNstrain = eventStrainNstrain_->GetValue();
    config.useAudioVibration = !audioVibrationSamples_.empty() && audioVibrationSampleRateHz_ > 0.0;
    if (config.useAudioVibration) {
        const std::vector<float> acousticPressure =
            ResampleForSlowTime(audioVibrationSamples_, audioVibrationSampleRateHz_, config.prfHz);
        config.vibrationSamples = ApplyAcousticFiberCoupling(acousticPressure, config.prfHz);
        config.vibrationSampleRateHz = config.prfHz;
    }
    config.vibrationSourceName = audioVibrationName_;
    config.useCuda = useCuda_->GetValue();
    return config;
}

void MainFrame::OnSize(wxSizeEvent& event)
{
    Layout();
    Refresh(true);
    event.Skip();
}

void MainFrame::OnRun(wxCommandEvent&)
{
    if (workerRunning_) {
        stopRequested_ = true;
        runButton_->Enable(false);
        statusText_->SetLabel(UiText("正在停止连续模拟..."));
        return;
    }
    if (worker_.joinable()) {
        worker_.join();
    }

    const AcquisitionConfig config = ReadConfig();
    stopRequested_ = false;
    SetBusy(true);
    worker_ = std::thread(&MainFrame::RunSimulationWorker, this, config);
}

void MainFrame::OnChooseAudio(wxCommandEvent&)
{
    wxFileDialog dialog(
        this,
        UiText("选择振动源音频文件"),
        wxEmptyString,
        wxEmptyString,
        UiText("音频文件 (*.mp3;*.wav;*.m4a;*.aac;*.wma)|*.mp3;*.wav;*.m4a;*.aac;*.wma|所有文件 (*.*)|*.*"),
        wxFD_OPEN | wxFD_FILE_MUST_EXIST);

    if (dialog.ShowModal() != wxID_OK) {
        return;
    }

    try {
        const DecodedAudio decoded = DecodeAudioFile(dialog.GetPath().ToStdWstring());
        audioVibrationSamples_ = decoded.samples;
        audioVibrationSampleRateHz_ = decoded.sampleRateHz;
        audioVibrationName_ = decoded.displayName;
        UpdateAudioSourceLabel();

        std::ostringstream status;
        status << "已加载音频振动源: " << audioVibrationName_ << ", "
               << audioVibrationSamples_.size() << " samples @ " << audioVibrationSampleRateHz_ << " Hz";
        if (workerRunning_) {
            status << "，重新开始连续模拟后生效";
        }
        statusText_->SetLabel(wxString::FromUTF8(status.str().c_str()));
    } catch (const std::exception& ex) {
        statusText_->SetLabel(wxString::Format("音频加载失败: %s", ex.what()));
    }
}

void MainFrame::OnPlayEventAudio(wxCommandEvent&)
{
    if (playEventAudio_->GetValue()) {
        if (currentResult_) {
            StartEventAudio(*currentResult_);
        } else {
            statusText_->SetLabel(UiText("请先运行模拟并在瀑布图中选择事件点。"));
            playEventAudio_->SetValue(false);
        }
    } else {
        StopEventAudio();
    }
}

void MainFrame::OnClose(wxCloseEvent& event)
{
    stopRequested_ = true;
    if (workerRunning_) {
        statusText_->SetLabel(UiText("正在等待后台解调结束..."));
    }
    StopEventAudio();
    if (worker_.joinable()) {
        worker_.join();
    }
    event.Skip();
}

void MainFrame::RunSimulationWorker(AcquisitionConfig config)
{
    try {
        DasSimulator simulator;
        DasDemodulator demodulator;
        double startTimeSec = 0.0;
        std::uint64_t frameIndex = 0;

        while (!stopRequested_) {
            const auto started = std::chrono::steady_clock::now();
            auto result = std::make_shared<DasResult>();
            AcquisitionFrame frame = simulator.Generate(config, startTimeSec);
            *result = demodulator.Process(frame, config);

            const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            std::ostringstream status;
            status << result->status << ", frame " << frameIndex << ", elapsed " << elapsed << " s";
            if (config.useAudioVibration) {
                status << ", vibration source " << config.vibrationSourceName;
            }
            result->status = status.str();

            CallAfter([this, result]() {
                ApplyResult(result);
            });

            startTimeSec += static_cast<double>(config.pulseCount) / config.prfHz;
            ++frameIndex;

            const auto workElapsed = std::chrono::steady_clock::now() - started;
            const auto targetPeriod = std::chrono::milliseconds(180);
            if (workElapsed < targetPeriod) {
                std::this_thread::sleep_for(targetPeriod - workElapsed);
            }
        }
    } catch (const std::exception& ex) {
        auto result = std::make_shared<DasResult>();
        result->status = wxString::Format("Simulation failed: %s", ex.what()).ToStdString();
        CallAfter([this, result]() {
            ApplyResult(result);
        });
    }

    CallAfter([this]() {
        SetBusy(false);
    });
}

void MainFrame::ApplyResult(std::shared_ptr<DasResult> result)
{
    RefreshEventSelection(*result);
    currentResult_ = result;
    rayleighPlot_->SetResult(result);
    waterfallPlot_->SetResult(result);
    tracePlot_->SetResult(result);
    spectrumPlot_->SetResult(result);
    statusText_->SetLabel(wxString::FromUTF8(result->status.c_str()));
    if (playEventAudio_->GetValue()) {
        if (!eventAudioStream_ || !eventAudioStream_->IsRunning()) {
            StartEventAudio(*result);
        } else {
            eventAudioStream_->PushEventTrace(result->eventTraceNstrain, result->config.prfHz);
        }
    }
    Layout();
    Refresh(true);
}

void MainFrame::OnWaterfallSelected(double selectedTimeSec, double selectedDistanceM)
{
    hasSelection_ = true;
    selectedDistanceM_ = selectedDistanceM;
    selectedTimeSec_ = selectedTimeSec;

    if (!currentResult_) {
        return;
    }

    RefreshEventSelection(*currentResult_);
    rayleighPlot_->SetResult(currentResult_);
    waterfallPlot_->SetResult(currentResult_);
    tracePlot_->SetResult(currentResult_);
    spectrumPlot_->SetResult(currentResult_);

    std::ostringstream status;
    status << currentResult_->status << ", selected by waterfall click at "
           << currentResult_->selectedDistanceM << " m / "
           << currentResult_->selectedTimeSec << " s";
    statusText_->SetLabel(wxString::FromUTF8(status.str().c_str()));

    if (playEventAudio_->GetValue()) {
        StartEventAudio(*currentResult_);
    }
}

void MainFrame::RefreshEventSelection(DasResult& result)
{
    DasDemodulator demodulator;
    if (hasSelection_) {
        double selectionTimeSec = selectedTimeSec_;
        if (!result.slowTimeSec.empty() &&
            (selectionTimeSec < result.slowTimeSec.front() || selectionTimeSec > result.slowTimeSec.back())) {
            selectionTimeSec = 0.5 * (static_cast<double>(result.slowTimeSec.front()) + static_cast<double>(result.slowTimeSec.back()));
        }
        demodulator.UpdateEventSelection(result, selectedDistanceM_, selectionTimeSec);
        selectedTimeSec_ = result.selectedTimeSec;
    } else {
        selectedDistanceM_ = result.selectedDistanceM;
        selectedTimeSec_ = result.selectedTimeSec;
    }
}

void MainFrame::StartEventAudio(const DasResult& result)
{
    StopEventAudio();

    eventAudioStream_ = std::make_unique<AudioStreamPlayer>();
    if (!eventAudioStream_->Start(kEventAudioSampleRateHz)) {
        eventAudioStream_.reset();
        statusText_->SetLabel(UiText("事件点音频流打开失败。"));
        playEventAudio_->SetValue(false);
        return;
    }

    eventAudioStream_->PushEventTrace(result.eventTraceNstrain, result.config.prfHz);
}

void MainFrame::StopEventAudio()
{
    eventAudioStream_.reset();
}

void MainFrame::UpdateAudioSourceLabel()
{
    if (!audioSourceText_) {
        return;
    }

    if (audioVibrationName_.empty()) {
        audioSourceText_->SetLabel(UiText("音频源：未选择"));
        return;
    }

    const double durationSec = audioVibrationSampleRateHz_ > 0.0
                                   ? static_cast<double>(audioVibrationSamples_.size()) / audioVibrationSampleRateHz_
                                   : 0.0;
    audioSourceText_->SetLabel(wxString::Format(
        UiText("外部声源：%s\n%.1f s @ %.0f Hz，整段参与仿真循环"),
        wxString::FromUTF8(audioVibrationName_.c_str()),
        durationSec,
        audioVibrationSampleRateHz_));
}

void MainFrame::SetBusy(bool busy)
{
    workerRunning_ = busy;
    runButton_->Enable(true);
    runButton_->SetLabel(busy ? UiText("停止连续模拟") : UiText("开始连续模拟"));
    if (busy) {
        statusText_->SetLabel(UiText("正在连续生成模拟 DMA 数据并解调..."));
    }
}
