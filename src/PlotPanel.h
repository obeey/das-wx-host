#pragma once

#include "DasTypes.h"

#include <wx/panel.h>

#include <memory>
#include <string>

enum class PlotKind {
    RayleighTrace,
    Waterfall,
    EventTrace,
    Spectrum
};

class PlotPanel final : public wxPanel {
public:
    PlotPanel(wxWindow* parent, PlotKind kind, wxString title);

    void SetResult(std::shared_ptr<const DasResult> result);

private:
    void OnPaint(wxPaintEvent& event);
    void OnSize(wxSizeEvent& event);
    void DrawAxes(wxDC& dc, const wxRect& rect, const wxString& xLabel, const wxString& yLabel);
    void DrawLine(wxDC& dc, const wxRect& rect, const std::vector<float>& x, const std::vector<float>& y);
    void DrawWaterfall(wxDC& dc, const wxRect& rect, const DasResult& result);
    wxColour ColorMap(float value) const;

    PlotKind kind_;
    wxString title_;
    std::shared_ptr<const DasResult> result_;
};
