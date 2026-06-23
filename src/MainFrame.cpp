#include "MainFrame.h"

#include "DasDemodulator.h"
#include "DasSimulator.h"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <algorithm>
#include <chrono>
#include <exception>
#include <sstream>

namespace {
constexpr int kRunButtonId = wxID_HIGHEST + 11;

wxString UiText(const char* text)
{
    return wxString::FromUTF8(text);
}
}

MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, UiText("DAS 上位机 - 仿真版"), wxDefaultPosition, wxSize(1380, 860))
{
    BuildUi();
    Bind(wxEVT_BUTTON, &MainFrame::OnRun, this, kRunButtonId);
    Bind(wxEVT_CLOSE_WINDOW, &MainFrame::OnClose, this);
}

MainFrame::~MainFrame()
{
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
    aomCenterMHz_ = AddSpin(leftPanel, left, UiText("AOM 中心 MHz"), 80.0, 10.0, 400.0, 1.0, 1);
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

    runButton_ = new wxButton(leftPanel, kRunButtonId, UiText("运行模拟采集/解调"));
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
    config.aomCenterFrequencyHz = aomCenterMHz_->GetValue() * 1.0e6;
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
    config.useCuda = useCuda_->GetValue();
    return config;
}

void MainFrame::OnRun(wxCommandEvent&)
{
    if (workerRunning_) {
        return;
    }
    if (worker_.joinable()) {
        worker_.join();
    }

    const AcquisitionConfig config = ReadConfig();
    SetBusy(true);
    worker_ = std::thread(&MainFrame::RunSimulationWorker, this, config);
}

void MainFrame::OnClose(wxCloseEvent& event)
{
    if (workerRunning_) {
        statusText_->SetLabel(UiText("正在等待后台解调结束..."));
    }
    if (worker_.joinable()) {
        worker_.join();
    }
    event.Skip();
}

void MainFrame::RunSimulationWorker(AcquisitionConfig config)
{
    workerRunning_ = true;
    auto result = std::make_shared<DasResult>();
    try {
        const auto started = std::chrono::steady_clock::now();
        DasSimulator simulator;
        DasDemodulator demodulator;
        AcquisitionFrame frame = simulator.Generate(config);
        *result = demodulator.Process(frame, config);
        const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        std::ostringstream status;
        status << result->status << ", elapsed " << elapsed << " s";
        result->status = status.str();
    } catch (const std::exception& ex) {
        result->status = wxString::Format("Simulation failed: %s", ex.what()).ToStdString();
    }

    CallAfter([this, result]() {
        ApplyResult(result);
        SetBusy(false);
    });
}

void MainFrame::ApplyResult(std::shared_ptr<DasResult> result)
{
    rayleighPlot_->SetResult(result);
    waterfallPlot_->SetResult(result);
    tracePlot_->SetResult(result);
    spectrumPlot_->SetResult(result);
    statusText_->SetLabel(wxString::FromUTF8(result->status.c_str()));
}

void MainFrame::SetBusy(bool busy)
{
    workerRunning_ = busy;
    runButton_->Enable(!busy);
    statusText_->SetLabel(busy ? UiText("正在生成模拟 DMA 数据并解调...") : statusText_->GetLabel());
}
