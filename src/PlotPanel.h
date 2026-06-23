#pragma once

#include "DasTypes.h"

#include <wx/panel.h>

#include <functional>
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
    void SetWaterfallSelectionHandler(std::function<void(double, double)> handler);

private:
    void OnPaint(wxPaintEvent& event);
    void OnSize(wxSizeEvent& event);
    void OnLeftDown(wxMouseEvent& event);
    void DrawAxes(wxDC& dc,
                  const wxRect& rect,
                  const wxString& xLabel,
                  const wxString& yLabel,
                  float minX,
                  float maxX,
                  float minY,
                  float maxY);
    void DrawLine(wxDC& dc, const wxRect& rect, const std::vector<float>& x, const std::vector<float>& y);
    void DrawWaterfall(wxDC& dc, const wxRect& rect, const DasResult& result);
    void DrawWaterfallSelection(wxDC& dc, const wxRect& rect, const DasResult& result);
    wxRect GetPlotRect() const;
    wxColour ColorMap(float value) const;

    PlotKind kind_;
    wxString title_;
    std::shared_ptr<const DasResult> result_;
    std::function<void(double, double)> waterfallSelectionHandler_;
};
