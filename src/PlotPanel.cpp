#include "PlotPanel.h"

#include <wx/dcbuffer.h>
#include <wx/event.h>

#include <algorithm>
#include <cmath>

namespace {
struct PlotRange {
    float minX = 0.0f;
    float maxX = 1.0f;
    float minY = 0.0f;
    float maxY = 1.0f;
};

void ExpandFlatRange(float& minValue, float& maxValue)
{
    if (std::abs(maxValue - minValue) >= 1.0e-6f) {
        return;
    }

    const float padding = std::max(1.0f, std::abs(minValue) * 0.05f);
    minValue -= padding;
    maxValue += padding;
}

PlotRange LineRange(const std::vector<float>& x, const std::vector<float>& y)
{
    PlotRange range;
    if (x.empty() || x.size() != y.size()) {
        return range;
    }

    const auto [minXIt, maxXIt] = std::minmax_element(x.begin(), x.end());
    const auto [minYIt, maxYIt] = std::minmax_element(y.begin(), y.end());
    range.minX = *minXIt;
    range.maxX = *maxXIt;
    range.minY = *minYIt;
    range.maxY = *maxYIt;
    ExpandFlatRange(range.minX, range.maxX);
    ExpandFlatRange(range.minY, range.maxY);
    return range;
}

wxString FormatTick(float value)
{
    const float magnitude = std::abs(value);
    if (magnitude >= 100.0f || magnitude < 0.01f) {
        return wxString::Format("%.0f", value);
    }
    if (magnitude >= 10.0f) {
        return wxString::Format("%.1f", value);
    }
    return wxString::Format("%.2f", value);
}

int ClampTextLeft(int x, int width, const wxRect& bounds)
{
    return std::clamp(x, bounds.GetLeft(), bounds.GetRight() - width + 1);
}
}

PlotPanel::PlotPanel(wxWindow* parent, PlotKind kind, wxString title)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_SIMPLE),
      kind_(kind),
      title_(std::move(title))
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    Bind(wxEVT_PAINT, &PlotPanel::OnPaint, this);
    Bind(wxEVT_SIZE, &PlotPanel::OnSize, this);
    Bind(wxEVT_LEFT_DOWN, &PlotPanel::OnLeftDown, this);
}

void PlotPanel::SetResult(std::shared_ptr<const DasResult> result)
{
    result_ = std::move(result);
    Refresh();
}

void PlotPanel::SetWaterfallSelectionHandler(std::function<void(double, double)> handler)
{
    waterfallSelectionHandler_ = std::move(handler);
}

void PlotPanel::OnSize(wxSizeEvent& event)
{
    Refresh(true);
    event.Skip();
}

void PlotPanel::OnPaint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(wxColour(250, 251, 252)));
    dc.Clear();

    const wxRect plotRect = GetPlotRect();

    dc.SetTextForeground(wxColour(35, 42, 52));
    dc.SetFont(wxFontInfo(10).Bold());
    dc.DrawText(title_, 10, 6);

    if (!result_) {
        dc.SetFont(wxFontInfo(9));
        dc.DrawText("Run simulation to populate this plot.", 16, 34);
        return;
    }

    switch (kind_) {
    case PlotKind::RayleighTrace:
        {
            std::vector<float> x(result_->rayleighDb.size());
            for (std::size_t i = 0; i < x.size(); ++i) {
                x[i] = static_cast<float>(result_->config.simulationStartM + static_cast<double>(i) * result_->config.dzM);
            }
            const PlotRange range = LineRange(x, result_->rayleighDb);
            DrawAxes(dc, plotRect, "Distance (m)", "Amplitude (dB)", range.minX, range.maxX, range.minY, range.maxY);
            DrawLine(dc, plotRect, x, result_->rayleighDb);
        }
        break;
    case PlotKind::Waterfall:
        DrawWaterfall(dc, plotRect, *result_);
        {
            const float minX = !result_->slowTimeSec.empty() ? result_->slowTimeSec.front() : 0.0f;
            const float maxX = !result_->slowTimeSec.empty()
                                   ? result_->slowTimeSec.back()
                                   : static_cast<float>(std::max<std::size_t>(1, result_->pulseCount) - 1) /
                                         static_cast<float>(std::max(1.0, result_->config.prfHz));
            const float minY = !result_->distanceM.empty() ? result_->distanceM.front() : static_cast<float>(result_->config.simulationStartM);
            const float maxY = !result_->distanceM.empty() ? result_->distanceM.back() : static_cast<float>(result_->config.simulationStopM);
            DrawAxes(dc, plotRect, "Time (s)", "Distance (m)", minX, maxX, minY, maxY);
        }
        DrawWaterfallSelection(dc, plotRect, *result_);
        break;
    case PlotKind::EventTrace:
        {
            const PlotRange range = LineRange(result_->eventTimeSec, result_->eventTraceNstrain);
            DrawAxes(dc, plotRect, "Time (s)", "Strain (nstrain)", range.minX, range.maxX, range.minY, range.maxY);
        }
        DrawLine(dc, plotRect, result_->eventTimeSec, result_->eventTraceNstrain);
        break;
    case PlotKind::Spectrum:
        {
            const PlotRange range = LineRange(result_->spectrumHz, result_->spectrumDb);
            DrawAxes(dc, plotRect, "Frequency (Hz)", "Amplitude (dB)", range.minX, range.maxX, range.minY, range.maxY);
        }
        DrawLine(dc, plotRect, result_->spectrumHz, result_->spectrumDb);
        break;
    }
}

void PlotPanel::OnLeftDown(wxMouseEvent& event)
{
    if (kind_ != PlotKind::Waterfall || !result_ || !waterfallSelectionHandler_) {
        event.Skip();
        return;
    }

    const wxRect plotRect = GetPlotRect();
    if (!plotRect.Contains(event.GetPosition())) {
        event.Skip();
        return;
    }

    const float minTime = !result_->slowTimeSec.empty() ? result_->slowTimeSec.front() : 0.0f;
    const float maxTime = !result_->slowTimeSec.empty()
                              ? result_->slowTimeSec.back()
                              : static_cast<float>(std::max<std::size_t>(1, result_->pulseCount) - 1) /
                                    static_cast<float>(std::max(1.0, result_->config.prfHz));
    const float minDistance = !result_->distanceM.empty() ? result_->distanceM.front() : static_cast<float>(result_->config.simulationStartM);
    const float maxDistance = !result_->distanceM.empty() ? result_->distanceM.back() : static_cast<float>(result_->config.simulationStopM);
    const double tx = static_cast<double>(event.GetX() - plotRect.GetLeft()) / std::max(1, plotRect.GetWidth());
    const double ty = static_cast<double>(plotRect.GetBottom() - event.GetY()) / std::max(1, plotRect.GetHeight());

    waterfallSelectionHandler_(minTime + tx * (maxTime - minTime), minDistance + ty * (maxDistance - minDistance));
}

void PlotPanel::DrawAxes(wxDC& dc,
                         const wxRect& rect,
                         const wxString& xLabel,
                         const wxString& yLabel,
                         float minX,
                         float maxX,
                         float minY,
                         float maxY)
{
    ExpandFlatRange(minX, maxX);
    ExpandFlatRange(minY, maxY);

    dc.SetFont(wxFontInfo(8));
    dc.SetTextForeground(wxColour(79, 89, 102));

    constexpr int tickCount = 5;
    dc.SetPen(wxPen(wxColour(225, 230, 236), 1));
    for (int i = 0; i < tickCount; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(tickCount - 1);
        const int x = rect.GetLeft() + static_cast<int>(std::round(t * rect.GetWidth()));
        const int y = rect.GetBottom() - static_cast<int>(std::round(t * rect.GetHeight()));
        dc.DrawLine(x, rect.GetTop(), x, rect.GetBottom());
        dc.DrawLine(rect.GetLeft(), y, rect.GetRight(), y);
    }

    dc.SetPen(wxPen(wxColour(176, 185, 196), 1));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawRectangle(rect);

    dc.SetPen(wxPen(wxColour(118, 130, 145), 1));
    const wxRect panelBounds(GetClientRect());
    for (int i = 0; i < tickCount; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(tickCount - 1);
        const int x = rect.GetLeft() + static_cast<int>(std::round(t * rect.GetWidth()));
        const int y = rect.GetBottom() - static_cast<int>(std::round(t * rect.GetHeight()));

        dc.DrawLine(x, rect.GetBottom(), x, rect.GetBottom() + 4);
        const wxString xTick = FormatTick(static_cast<float>(minX + t * (maxX - minX)));
        const wxSize xTickSize = dc.GetTextExtent(xTick);
        dc.DrawText(xTick, ClampTextLeft(x - xTickSize.GetWidth() / 2, xTickSize.GetWidth(), panelBounds), rect.GetBottom() + 6);

        dc.DrawLine(rect.GetLeft() - 4, y, rect.GetLeft(), y);
        const wxString yTick = FormatTick(static_cast<float>(minY + t * (maxY - minY)));
        const wxSize yTickSize = dc.GetTextExtent(yTick);
        dc.DrawText(yTick, rect.GetLeft() - yTickSize.GetWidth() - 8, y - yTickSize.GetHeight() / 2);
    }

    const wxSize xLabelSize = dc.GetTextExtent(xLabel);
    dc.DrawText(xLabel, rect.GetLeft() + rect.GetWidth() / 2 - xLabelSize.GetWidth() / 2, rect.GetBottom() + 28);
    dc.DrawRotatedText(yLabel, 8, rect.GetTop() + rect.GetHeight() / 2 + 42, 90.0);
}

void PlotPanel::DrawLine(wxDC& dc, const wxRect& rect, const std::vector<float>& x, const std::vector<float>& y)
{
    if (x.size() < 2 || x.size() != y.size()) {
        return;
    }

    const auto [minXIt, maxXIt] = std::minmax_element(x.begin(), x.end());
    const auto [minYIt, maxYIt] = std::minmax_element(y.begin(), y.end());
    float minX = *minXIt;
    float maxX = *maxXIt;
    float minY = *minYIt;
    float maxY = *maxYIt;
    ExpandFlatRange(minX, maxX);
    ExpandFlatRange(minY, maxY);

    const auto px = [&](float value) {
        return rect.GetLeft() + static_cast<int>((value - minX) / std::max(1.0e-12f, maxX - minX) * rect.GetWidth());
    };
    const auto py = [&](float value) {
        return rect.GetBottom() - static_cast<int>((value - minY) / std::max(1.0e-12f, maxY - minY) * rect.GetHeight());
    };

    dc.SetClippingRegion(rect);
    dc.SetPen(wxPen(wxColour(20, 107, 168), 2));
    const std::size_t stride = std::max<std::size_t>(1, y.size() / static_cast<std::size_t>(std::max(1, rect.GetWidth() * 2)));
    wxPoint previous(px(x[0]), py(y[0]));
    for (std::size_t i = stride; i < y.size(); i += stride) {
        wxPoint current(px(x[i]), py(y[i]));
        dc.DrawLine(previous, current);
        previous = current;
    }
    dc.DestroyClippingRegion();

}

void PlotPanel::DrawWaterfall(wxDC& dc, const wxRect& rect, const DasResult& result)
{
    if (result.rangeCount == 0 || result.pulseCount == 0 || result.dynamicStrainNstrain.empty()) {
        return;
    }

    wxImage image(rect.GetWidth(), rect.GetHeight(), false);
    unsigned char* data = image.GetData();
    for (int y = 0; y < rect.GetHeight(); ++y) {
        const std::size_t iz = std::min<std::size_t>(
            result.rangeCount - 1,
            static_cast<std::size_t>((static_cast<double>(rect.GetHeight() - 1 - y) / std::max(1, rect.GetHeight() - 1)) * (result.rangeCount - 1)));
        for (int x = 0; x < rect.GetWidth(); ++x) {
            const std::size_t ip = std::min<std::size_t>(
                result.pulseCount - 1,
                static_cast<std::size_t>((static_cast<double>(x) / std::max(1, rect.GetWidth() - 1)) * (result.pulseCount - 1)));
            const float v = result.dynamicStrainNstrain[iz * result.pulseCount + ip];
            const wxColour c = ColorMap(std::clamp(v / 120.0f, -1.0f, 1.0f));
            const int offset = (y * rect.GetWidth() + x) * 3;
            data[offset + 0] = c.Red();
            data[offset + 1] = c.Green();
            data[offset + 2] = c.Blue();
        }
    }
    dc.DrawBitmap(wxBitmap(image), rect.GetTopLeft());
}

void PlotPanel::DrawWaterfallSelection(wxDC& dc, const wxRect& rect, const DasResult& result)
{
    if (result.slowTimeSec.empty() || result.distanceM.empty()) {
        return;
    }

    const double minTime = result.slowTimeSec.front();
    const double maxTime = result.slowTimeSec.back();
    const double minDistance = result.distanceM.front();
    const double maxDistance = result.distanceM.back();
    if (result.selectedTimeSec < minTime || result.selectedTimeSec > maxTime ||
        result.selectedDistanceM < minDistance || result.selectedDistanceM > maxDistance) {
        return;
    }

    const int x = rect.GetLeft() + static_cast<int>(
        (result.selectedTimeSec - minTime) / std::max(1.0e-12, maxTime - minTime) * rect.GetWidth());
    const int y = rect.GetBottom() - static_cast<int>(
        (result.selectedDistanceM - minDistance) / std::max(1.0e-12, maxDistance - minDistance) * rect.GetHeight());

    dc.SetClippingRegion(rect);
    dc.SetPen(wxPen(wxColour(20, 22, 25), 1, wxPENSTYLE_SHORT_DASH));
    dc.DrawLine(x, rect.GetTop(), x, rect.GetBottom());
    dc.DrawLine(rect.GetLeft(), y, rect.GetRight(), y);
    dc.SetBrush(wxBrush(wxColour(255, 255, 255)));
    dc.SetPen(wxPen(wxColour(20, 22, 25), 1));
    dc.DrawCircle(x, y, 4);
    dc.DestroyClippingRegion();
}

wxRect PlotPanel::GetPlotRect() const
{
    const wxSize size = GetClientSize();
    return wxRect(76, 28, std::max(10, size.GetWidth() - 102), std::max(10, size.GetHeight() - 76));
}

wxColour PlotPanel::ColorMap(float value) const
{
    const float t = std::clamp((value + 1.0f) * 0.5f, 0.0f, 1.0f);
    const float r = std::clamp(1.5f - std::abs(4.0f * t - 3.0f), 0.0f, 1.0f);
    const float g = std::clamp(1.5f - std::abs(4.0f * t - 2.0f), 0.0f, 1.0f);
    const float b = std::clamp(1.5f - std::abs(4.0f * t - 1.0f), 0.0f, 1.0f);
    return wxColour(static_cast<unsigned char>(255.0f * r),
                    static_cast<unsigned char>(255.0f * g),
                    static_cast<unsigned char>(255.0f * b));
}
