#pragma once
#include "wlan_session.hpp"
#include <pspctrl.h>
#include <vector>
class SourceVM;
namespace doubleedged {
struct WlanProbe {unsigned status,role,frame,confirmed,checksum,error;};
class WlanFrontend {
    unsigned confirmed_=0,pending_buttons_=0,last_buttons_=0;
    bool qa_=false;
    std::vector<unsigned char> scripted_;
public:
    WlanSession session;
    bool connect(SourceVM& vm,bool qa,volatile bool& exiting);
    bool apply_profile(SourceVM& vm);
    void sample(const SceCtrlData& pad);
    bool checksum(SourceVM& vm);
    void probe(unsigned hash=0);
    static const char* status_text(WlanStatus status);
};
}
extern "C" {extern volatile doubleedged::WlanProbe g_double_edged_link;}
