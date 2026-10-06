#include "../src/Keyboard.h"
#include <wrl.h>
#include <future>
#include <iostream>
#include <stdexcept>

using namespace island;
static void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
int main(){
    HWND root=nullptr;
    try{
        root=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"STATIC",nullptr,WS_POPUP,0,0,320,160,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        HWND edit=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|ES_MULTILINE,0,0,100,24,root,nullptr,GetModuleHandleW(nullptr),nullptr);
        HWND password=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|ES_PASSWORD,0,30,100,24,root,nullptr,GetModuleHandleW(nullptr),nullptr);
        HWND button=CreateWindowExW(0,L"BUTTON",L"",WS_CHILD,0,60,100,24,root,nullptr,GetModuleHandleW(nullptr),nullptr);
        require(root&&edit&&password&&button,"Could not create hidden native focus fixtures");
        require(nativeTextFocus(edit,nullptr)&&nativeTextFocus(password,nullptr)&&nativeTextFocus(button,edit)&&!nativeTextFocus(button,nullptr),"Native edit/password/caret checks failed");
        // The UIA client runs separately while this thread pumps the hidden HWND
        // providers. No foreground activation, injected input or real player commands.
        auto verification=std::async(std::launch::async,[=]{
            require(SUCCEEDED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)),"Could not initialize the accessibility worker");
            int checks=0;
            {
                Microsoft::WRL::ComPtr<IUIAutomation> automation;require(SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation8,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&automation))),"Could not initialize native UI Automation");
                for(auto fixture:{std::pair{root,true},std::pair{button,true},std::pair{edit,false},std::pair{password,false}}){
                    Microsoft::WRL::ComPtr<IUIAutomationElement> element;require(SUCCEEDED(automation->ElementFromHandle(fixture.first,&element)),"The native focus fixture was not accessible");
                    require(automationAllowsArrows(element.Get())==fixture.second,"Native accessibility misclassified a non-editable window/button or text/password field");checks++;
                }
                require(!automationAllowsArrows(nullptr),"Unknown accessibility focus was approved");checks++;
            }
            CoUninitialize();return checks;
        });
        while(verification.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}MsgWaitForMultipleObjectsEx(0,nullptr,10,QS_ALLINPUT,MWMO_INPUTAVAILABLE);}
        auto checks=verification.get();DestroyWindow(root);root=nullptr;
        std::cout<<"PASS: "<<checks<<" real UI Automation window/button/edit/password classification checks, plus native caret protection\n";return 0;
    }catch(const std::exception& error){if(root)DestroyWindow(root);std::cerr<<error.what()<<'\n';return 1;}
}
