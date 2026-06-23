#include "PlotPanel.h"

#include <wx/dcbuffer.h>

#include <algorithm>
#include <cmath>

PlotPanel::PlotPanel(wxWindow* parent, PlotKind kind, wxString title)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_SIMPLE),
      kind_(kind),
      title_(std::move(title))
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    Bind(wxEVT_PAINT, &PlotPanel::OnPaint, this);
}

void PlotPanel::SetResult(std::shared_ptr<const DasResult> result)
{
    result_ = std::move(result);
    Refresh();
}

void PlotPanel::OnPaint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(wxColour(250, 251, 252)));
    dc.Clear();

    const wxSize size = GetClientSize();
    wxRect plotRect(58, 28, std::max(10, size.GetWidth() - 76), std::max(10, size.GetHeight() - 64));

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
        DrawAxes(dc, plotRect, "Distance (m)", "Amplitude (dB)");
        DrawLine(dc, plotRect, result_->config.simulationStopM > 0 ? result_->config.dzM > 0 ? std::vector<float>() : std::vector<float>() : std::vector<float>(), result_->rayleighDb);
        {
            std::vector<float> x(result_->rayleighDb.size());
            for (std::size_t i = 0; i < x.size(); ++i) {
                x[i] = static_cast<float>(result_->config.simulationStartM + static_cast<double>(i) * result_->config.dzM);
            }
            DrawLine(dc, plotRect, x, result_->rayleighDb);
        }
        break;
    case PlotKind::Waterfall:
        DrawAxes(dc, plotRect, "Time (s)", "Distance (m)");
        DrawWaterfall(dc, plotRect, *result_);
        break;
    case PlotKind::EventTrace:
        DrawAxes(dc, plotRect, "Time (s)", "Strain (nstrain)");
        DrawLine(dc, plotRect, result_->eventTimeSec, result_->eventTraceNstrain);
        break;
    case PlotKind::Spectrum:
        DrawAxes(dc, plotRect, "Frequency (Hz)", "Amplitude (dB)");
        DrawLine(dc, plotRect, result_->spectrumHz, result_->spectrumDb);
        break;
    }
}

void PlotPanel::DrawAxes(wxDC& dc, const wxRect& rect, const wxString& xLabel, const wxString& yLabel)
{
    dc.SetPen(wxPen(wxColour(176, 185, 196), 1));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawRectangle(rect);
    dc.SetFont(wxFontInfo(8));
    dc.SetTextForeground(wxColour(79, 89, 102));
    dc.DrawText(xLabel, rect.GetLeft() + rect.GetWidth() / 2 - 34, rect.GetBottom() + 20);
    dc.DrawRotatedText(yLabel, 8, rect.GetTop() + rect.GetHeight() / 2 + 42, 90.0);
}

void PlotPanel::DrawLine(wxDC& dc, const wxRect& rect, const std::vector<float>& x, const std::vector<float>& y)
{
    if (x.size() < 2 || x.size() != y.size()) {
        return;
    }

    const auto [minXIt, maxXIt] = std::minmax_element(x.begin(), x.end());
    const auto [minYIt, maxYIt] = std::minmax_element(y.begin(), y.end());
    const float minX = *minXIt;
    const float maxX = *maxXIt;
    float minY = *minYIt;
    float maxY = *maxYIt;
    if (std::abs(maxY - minY) < 1.0e-6f) {
        minY -= 1.0f;
        maxY += 1.0f;
    }

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

    dc.SetFont(wxFontInfo(8));
    dc.SetTextForeground(wxColour(79, 89, 102));
    dc.DrawText(wxString::Format("%.3g", minX), rect.GetLeft() - 4, rect.GetBottom() + 4);
    dc.DrawText(wxString::Format("%.3g", maxX), rect.GetRight() - 34, rect.GetBottom() + 4);
    dc.DrawText(wxString::Format("%.3g", maxY), rect.GetLeft() - 50, rect.GetTop() - 2);
    dc.DrawText(wxString::Format("%.3g", minY), rect.GetLeft() - 50, rect.GetBottom() - 12);
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
