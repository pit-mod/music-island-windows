#include "Audio.h"
#include "Media.h"
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <appmodel.h>
#include <wrl.h>
#include <cwctype>
#include <vector>

namespace island {
using namespace Microsoft::WRL;
static std::wstring lower(std::wstring value) {for(auto& c:value)c=static_cast<wchar_t>(std::towlower(c));return value;}
static bool sourceMatches(std::wstring source,DWORD pid) {
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(!process)return false;
    source=lower(source);bool match=false;UINT length=0;
    if(GetApplicationUserModelId(process,&length,nullptr)==ERROR_INSUFFICIENT_BUFFER&&length>0&&length<1024) {
        std::wstring id(length,L'\0');if(GetApplicationUserModelId(process,&length,id.data())==ERROR_SUCCESS){id.resize(length-1);match=lower(id)==source;}
    }
    wchar_t path[32768];DWORD count=32768;
    if(!match&&QueryFullProcessImageNameW(process,0,path,&count)) {
        std::wstring name=lower(path);name=name.substr(name.find_last_of(L"\\/")+1);if(name.ends_with(L".exe"))name.resize(name.size()-4);
        auto base=source;if(base.ends_with(L".exe"))base.resize(base.size()-4);match=base==name;
        if(!match)for(const auto& pair:{std::pair{L"chrome",L"google.chrome"},std::pair{L"msedge",L"microsoft.edge"},std::pair{L"firefox",L"mozilla.firefox"},std::pair{L"brave",L"bravesoftware.bravebrowser"},std::pair{L"vlc",L"org.videolan.vlc"},std::pair{L"spotify",L"spotify"},std::pair{L"deezer",L"com.deezer.deezer-desktop"}})
            if(name==pair.first&&(base==pair.second||base.starts_with(std::wstring(pair.second)+L".")))match=true;
    }
    CloseHandle(process);return match;
}
struct SessionMeter {DWORD pid=0;ComPtr<IAudioMeterInformation> meter;};
static std::vector<SessionMeter> findSessions(IMMDeviceEnumerator* enumerator,const std::wstring& source) {
    std::vector<SessionMeter> result;ComPtr<IMMDeviceCollection> devices;
    if(FAILED(enumerator->EnumAudioEndpoints(eRender,DEVICE_STATE_ACTIVE,&devices)))return result;
    UINT count=0;devices->GetCount(&count);
    for(UINT d=0;d<count;d++) {
        ComPtr<IMMDevice> device;ComPtr<IAudioSessionManager2> manager;ComPtr<IAudioSessionEnumerator> sessions;
        if(FAILED(devices->Item(d,&device))||FAILED(device->Activate(__uuidof(IAudioSessionManager2),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(manager.GetAddressOf())))||FAILED(manager->GetSessionEnumerator(&sessions)))continue;
        int size=0;sessions->GetCount(&size);
        for(int i=0;i<size;i++) {
            ComPtr<IAudioSessionControl> control;ComPtr<IAudioSessionControl2> detail;SessionMeter value;
            if(SUCCEEDED(sessions->GetSession(i,&control))&&SUCCEEDED(control.As(&detail))&&SUCCEEDED(detail->GetProcessId(&value.pid))&&value.pid&&sourceMatches(source,value.pid)&&SUCCEEDED(control.As(&value.meter)))result.push_back(value);
        }
    }
    return result;
}
AudioVisualizer::~AudioVisualizer(){{std::lock_guard lock(mutex);stopping=true;}wake.notify_all();if(worker.joinable())worker.join();}
void AudioVisualizer::select(const std::wstring& source,bool active){std::lock_guard lock(mutex);if(requested!=source||playing!=active){requested=source;playing=active;wake.notify_all();}}
AudioFrame AudioVisualizer::read(){std::lock_guard lock(mutex);return latest;}
void AudioVisualizer::run() {
    HRESULT initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);if(FAILED(initialized))return;
    {
        ComPtr<IMMDeviceEnumerator> enumerator;CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&enumerator));
        PeakHistory history;unsigned long long samples=0;std::vector<SessionMeter> sessions;std::wstring source;double refresh=0;
        while(true) {
            std::wstring selected;bool active;
            {std::unique_lock lock(mutex);wake.wait_for(lock,std::chrono::milliseconds(25),[&]{return stopping;});if(stopping)break;selected=requested;active=playing;}
            double now=clockSeconds();AudioFrame state;
            if(selected!=source){source=selected;history.reset();samples=0;sessions.clear();refresh=0;}
            if(active&&!source.empty()&&enumerator) {
                if(now>=refresh){refresh=now+1;sessions=findSessions(enumerator.Get(),source);}
                DWORD loudest=0;double best=-1;
                for(auto& s:sessions){float peak=0;if(SUCCEEDED(s.meter->GetPeakValue(&peak))&&std::isfinite(peak)){state.peak=std::max(state.peak,static_cast<double>(peak));if(peak>best){best=peak;loudest=s.pid;}}}
                state.matches=static_cast<unsigned>(sessions.size());
                state.available=!sessions.empty();state.process=loudest;history.append(static_cast<float>(state.peak),state.available);samples++;state.peaks=history.display();state.samples=samples;
            }else {history.reset();samples=0;sessions.clear();refresh=0;}
            state.source=source;
            state.received=clockSeconds();{std::lock_guard lock(mutex);latest=state;}
        }
    }
    CoUninitialize();
}
}
