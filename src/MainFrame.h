#pragma once

#include "DasTypes.h"
#include "PlotPanel.h"

#include <wx/frame.h>
#include <wx/spinctrl.h>

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

class wxButton;
class wxCheckBox;
class wxStaticText;
class AudioStreamPlayer;

class MainFrame final : public wxFrame {
public:
    MainFrame();
    ~MainFrame() override;

private:
    wxSpinCtrlDouble* AddSpin(wxWindow* parent,
                              wxSizer* sizer,
                              const wxString& label,
                              double value,
                              double minValue,
                              double maxValue,
                              double increment,
                              int digits = 2);
    void BuildUi();
    AcquisitionConfig ReadConfig() const;
    void OnSize(wxSizeEvent& event);
    void OnRun(wxCommandEvent& event);
    void OnChooseAudio(wxCommandEvent& event);
    void OnPlayEventAudio(wxCommandEvent& event);
    void OnClose(wxCloseEvent& event);
    void OnWaterfallSelected(double selectedTimeSec, double selectedDistanceM);
    void RunSimulationWorker(AcquisitionConfig config);
    void ApplyResult(std::shared_ptr<DasResult> result);
    void RefreshEventSelection(DasResult& result);
    void StartEventAudio(const DasResult& result);
    void StopEventAudio();
    void UpdateAudioSourceLabel();
    void SetBusy(bool busy);

    wxSpinCtrlDouble* chirpBandwidthMHz_ = nullptr;
    wxSpinCtrlDouble* chirpDurationUs_ = nullptr;
    wxSpinCtrlDouble* aomStartMHz_ = nullptr;
    wxSpinCtrlDouble* pulseWidthNs_ = nullptr;
    wxSpinCtrlDouble* adcGsps_ = nullptr;
    wxSpinCtrlDouble* fiberKm_ = nullptr;
    wxSpinCtrlDouble* simStopM_ = nullptr;
    wxSpinCtrlDouble* dzM_ = nullptr;
    wxSpinCtrlDouble* gaugeM_ = nullptr;
    wxSpinCtrlDouble* prfHz_ = nullptr;
    wxSpinCtrlDouble* pulseCount_ = nullptr;
    wxSpinCtrlDouble* snrDb_ = nullptr;
    wxSpinCtrlDouble* eventPositionM_ = nullptr;
    wxSpinCtrlDouble* eventFrequencyHz_ = nullptr;
    wxSpinCtrlDouble* eventStrainNstrain_ = nullptr;
    wxCheckBox* useCuda_ = nullptr;
    wxButton* chooseAudioButton_ = nullptr;
    wxStaticText* audioSourceText_ = nullptr;
    wxCheckBox* playEventAudio_ = nullptr;
    wxButton* runButton_ = nullptr;
    wxStaticText* statusText_ = nullptr;

    PlotPanel* rayleighPlot_ = nullptr;
    PlotPanel* waterfallPlot_ = nullptr;
    PlotPanel* tracePlot_ = nullptr;
    PlotPanel* spectrumPlot_ = nullptr;

    std::thread worker_;
    std::atomic_bool workerRunning_ = false;
    std::atomic_bool stopRequested_ = false;

    std::shared_ptr<DasResult> currentResult_;
    bool hasSelection_ = false;
    double selectedDistanceM_ = 0.0;
    double selectedTimeSec_ = 0.0;

    std::vector<float> audioVibrationSamples_;
    double audioVibrationSampleRateHz_ = 0.0;
    std::string audioVibrationName_;
    std::unique_ptr<AudioStreamPlayer> eventAudioStream_;
};
