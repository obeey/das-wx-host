#include "MainFrame.h"

#include <wx/app.h>

class DasWxApp final : public wxApp {
public:
    bool OnInit() override
    {
        if (!wxApp::OnInit()) {
            return false;
        }
        auto* frame = new MainFrame();
        frame->Show(true);
        frame->Maximize(true);
        frame->SendSizeEvent();
        return true;
    }
};

wxIMPLEMENT_APP(DasWxApp);
