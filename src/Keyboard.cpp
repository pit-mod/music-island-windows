#include "Keyboard.h"
#include <wrl.h>
#include <algorithm>
#include <cwctype>
#include <string>

namespace island {
using Microsoft::WRL::ComPtr;
class FocusObserver final : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,IUIAutomationFocusChangedEventHandler> {
    TypingGuard* guard;
public:
    explicit FocusObserver(TypingGuard* owner):guard(owner){}
    HRESULT STDMETHODCALLTYPE HandleFocusChangedEvent(IUIAutomationElement*) override {guard->invalidate();return S_OK;}
};
static std::wstring lower(std::wstring value){for(auto& c:value)c=static_cast<wchar_t>(std::towlower(c));return value;}
bool nativeTextFocus(HWND focused,HWND caret) {
    if(caret)return true;
    for(HWND current=focused;current;current=GetParent(current)){
        wchar_t buffer[256]{};GetClassNameW(current,buffer,256);auto name=lower(buffer);
        if(name.find(L"edit")!=std::wstring::npos||name.find(L"scintilla")!=std::wstring::npos||name==L"consolewindowclass"||name==L"cascadia_hosting_window_class")return true;
    }
    return false;
}
static bool propertyFlag(IUIAutomationElement* element,PROPERTYID property,bool& flag) {
    VARIANT value{};HRESULT result=element->GetCurrentPropertyValue(property,&value);
    bool known=SUCCEEDED(result)&&value.vt==VT_BOOL;if(known)flag=value.boolVal!=VARIANT_FALSE;VariantClear(&value);return known;
}
FocusKind automationFocusKind(IUIAutomationElement* element) {
    if(!element)return FocusKind::Unknown;
    FocusMetadata metadata;CONTROLTYPEID type=0;BOOL password=FALSE;
    if(FAILED(element->get_CurrentControlType(&type))||FAILED(element->get_CurrentIsPassword(&password)))return FocusKind::Unknown;
    metadata.password=password!=FALSE;metadata.edit=type==UIA_EditControlTypeId;
    if(metadata.password||metadata.edit)return FocusKind::Editing;
    metadata.known=type!=0;
    if(!propertyFlag(element,UIA_IsTextEditPatternAvailablePropertyId,metadata.textEdit)||!propertyFlag(element,UIA_IsValuePatternAvailablePropertyId,metadata.value)||!propertyFlag(element,UIA_IsTextPatternAvailablePropertyId,metadata.text))return FocusKind::Unknown;
    if(metadata.textEdit)return FocusKind::Editing;
    if(metadata.value){
        ComPtr<IUIAutomationValuePattern> pattern;BOOL readOnly=FALSE;
        if(SUCCEEDED(element->GetCurrentPatternAs(UIA_ValuePatternId,IID_PPV_ARGS(&pattern)))&&pattern&&SUCCEEDED(pattern->get_CurrentIsReadOnly(&readOnly))){metadata.valueKnown=true;metadata.valueReadOnly=readOnly!=FALSE;}
        if(!metadata.valueKnown||!metadata.valueReadOnly)return FocusKind::Editing;
    }
    if(metadata.text){
        ComPtr<IUIAutomationTextPattern> pattern;ComPtr<IUIAutomationTextRange> range;VARIANT readOnly{};
        if(SUCCEEDED(element->GetCurrentPatternAs(UIA_TextPatternId,IID_PPV_ARGS(&pattern)))&&pattern&&SUCCEEDED(pattern->get_DocumentRange(&range))&&range&&SUCCEEDED(range->GetAttributeValue(UIA_IsReadOnlyAttributeId,&readOnly))&&readOnly.vt==VT_BOOL){metadata.textKnown=true;metadata.textReadOnly=readOnly.boolVal!=VARIANT_FALSE;}
        VariantClear(&readOnly);
    }
    return classifyFocus(metadata);
}
bool automationAllowsArrows(IUIAutomationElement* element){return automationFocusKind(element)==FocusKind::Browsing;}
void TypingGuard::noteKey(unsigned key,bool down) {
    if(!down||!enabled)return;
    if((key>='A'&&key<='Z')||(key>='0'&&key<='9')||(key>=VK_NUMPAD0&&key<=VK_DIVIDE)||(key>=VK_OEM_1&&key<=VK_OEM_102)||key==VK_PACKET||key==VK_PROCESSKEY||key==VK_BACK||key==VK_DELETE||key==VK_SPACE||key==VK_RETURN||key==VK_TAB){typedAt=GetTickCount64();invalidate();}
}
bool TypingGuard::allowsArrows() {
    if(!enabled||(typedAt&&GetTickCount64()-typedAt<2000))return false;
    HWND foreground=GetForegroundWindow();GUITHREADINFO info{sizeof(info)};
    if(!foreground||!GetGUIThreadInfo(GetWindowThreadProcessId(foreground,nullptr),&info)||nativeTextFocus(info.hwndFocus,info.hwndCaret)||(info.flags&(GUI_INMENUMODE|GUI_INMOVESIZE)))return false;
    std::lock_guard lock(mutex);
    return sample.allows(reinterpret_cast<unsigned long long>(foreground),reinterpret_cast<unsigned long long>(info.hwndFocus),epoch.load(),GetTickCount64());
}
TypingGuard::~TypingGuard(){{std::lock_guard lock(mutex);stopping=true;}wake.notify_all();if(worker.joinable())worker.join();}
void TypingGuard::run() {
    if(FAILED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)))return;
    {
        ComPtr<IUIAutomation> automation;CoCreateInstance(CLSID_CUIAutomation8,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&automation));
        ComPtr<IUIAutomation2> timed;if(automation&&SUCCEEDED(automation.As(&timed))){timed->put_ConnectionTimeout(100);timed->put_TransactionTimeout(100);}
        auto focusObserver=Microsoft::WRL::Make<FocusObserver>(this);
        bool focusEvents=automation&&focusObserver&&SUCCEEDED(automation->AddFocusChangedEventHandler(nullptr,focusObserver.Get()));
        unsigned long long observed=0;
        while(true){
            {std::unique_lock lock(mutex);wake.wait_for(lock,std::chrono::milliseconds(enabled?75:1000),[&]{return stopping||epoch.load()!=observed;});if(stopping)break;observed=epoch.load();}
            if(!enabled)continue;
            FocusApproval next;next.epoch=observed;next.checked=GetTickCount64();HWND foreground=GetForegroundWindow();next.foreground=reinterpret_cast<unsigned long long>(foreground);GUITHREADINFO info{sizeof(info)};
            if(foreground&&GetGUIThreadInfo(GetWindowThreadProcessId(foreground,nullptr),&info)){
                next.focus=reinterpret_cast<unsigned long long>(info.hwndFocus);
                if(nativeTextFocus(info.hwndFocus,info.hwndCaret))next.kind=FocusKind::Editing;
                else if(foreground==islandWindow.load())next.kind=FocusKind::Browsing;
                else if(automation){
                    ComPtr<IUIAutomationElement> focused;
                    // Browser accessibility can live in a renderer process. GetFocusedElement
                    // already identifies global input focus; matching process IDs rejected it.
                    if(SUCCEEDED(automation->GetFocusedElement(&focused)))next.kind=automationFocusKind(focused.Get());
                }
            }
            if(foreground!=GetForegroundWindow()||next.epoch!=epoch.load()||GetTickCount64()-next.checked>=500)next.kind=FocusKind::Unknown;
            {std::lock_guard lock(mutex);sample=next;}
        }
        if(focusEvents)automation->RemoveFocusChangedEventHandler(focusObserver.Get());
    }
    CoUninitialize();
}
}
