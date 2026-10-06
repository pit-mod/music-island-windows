#pragma once
#include <windows.h>
#include <objbase.h>
#include <oleauto.h>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <UIAutomation.h>
#include "KeyboardPolicy.h"

namespace island {
// Inspects control metadata only. Never reads text, passwords, or clipboard data.
bool nativeTextFocus(HWND focused,HWND caret);
FocusKind automationFocusKind(IUIAutomationElement* element);
bool automationAllowsArrows(IUIAutomationElement* element);
class TypingGuard {
    std::mutex mutex;
    std::condition_variable wake;
    FocusApproval sample;
    std::atomic<unsigned long long> epoch{0};
    std::atomic<bool> enabled{false};
    std::atomic<HWND> islandWindow{nullptr};
    bool stopping=false;
    std::thread worker;
    ULONGLONG typedAt=0;
    void run();
public:
    TypingGuard():worker([this]{run();}){}
    ~TypingGuard();
    void setEnabled(bool value){enabled=value;invalidate();}
    void setWindow(HWND value){islandWindow=value;invalidate();}
    void invalidate(){++epoch;wake.notify_all();}
    void noteKey(unsigned key,bool down);
    bool allowsArrows();
};
}
