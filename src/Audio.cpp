#include "Audio.h"
#include "Spectrum.h"
#include "Media.h"
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <appmodel.h>
#include <wrl.h>
#include <wrl/implements.h>
#include <tlhelp32.h>
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
struct SessionMeter {DWORD pid=0;ComPtr<IAudioMeterInformation> meter;ComPtr<ISimpleAudioVolume> volume;ComPtr<IAudioEndpointVolume> endpoint;};
class LoopbackActivation final:public RuntimeClass<RuntimeClassFlags<ClassicCom>,FtmBase,IActivateAudioInterfaceCompletionHandler> {
public:
    HANDLE ready=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    HRESULT result=E_PENDING;
    ComPtr<IAudioClient> client;
    ~LoopbackActivation(){if(ready)CloseHandle(ready);}
    HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation* operation) override {
        HRESULT activated=E_FAIL;ComPtr<IUnknown> object;
        result=operation->GetActivateResult(&activated,&object);
        if(SUCCEEDED(result)){result=activated;if(SUCCEEDED(result))result=object.As(&client);}
        SetEvent(ready);return S_OK;
    }
};
// Reads the selected process tree after its stream/session volume controls.
// Samples are reduced to frequency-band energy; no audio is retained or written.
class ProcessLoopback {
    ComPtr<IAudioClient> client;
    ComPtr<IAudioCaptureClient> capture;
    HANDLE packets=nullptr;
    SpectrumAnalyzer spectrum;
public:
    DWORD process=0;
    ~ProcessLoopback(){reset();}
    void reset(){if(client)client->Stop();capture.Reset();client.Reset();if(packets)CloseHandle(packets);packets=nullptr;process=0;spectrum.reset();}
    AudioPeaks bands(float gain) const {return spectrum.display(gain);}
    HRESULT open(DWORD pid) {
        reset();auto activation=Make<LoopbackActivation>();if(!activation||!activation->ready)return E_OUTOFMEMORY;
        AUDIOCLIENT_ACTIVATION_PARAMS params{};params.ActivationType=AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
        params.ProcessLoopbackParams.TargetProcessId=pid;params.ProcessLoopbackParams.ProcessLoopbackMode=PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
        PROPVARIANT prop{};prop.vt=VT_BLOB;prop.blob.cbSize=sizeof(params);prop.blob.pBlobData=reinterpret_cast<BYTE*>(&params);
        ComPtr<IActivateAudioInterfaceAsyncOperation> operation;
        HRESULT hr=ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,__uuidof(IAudioClient),&prop,activation.Get(),&operation);
        if(FAILED(hr))return hr;
        if(WaitForSingleObject(activation->ready,5000)!=WAIT_OBJECT_0)return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        if(FAILED(activation->result))return activation->result;
        client=activation->client;
        WAVEFORMATEX format{};format.wFormatTag=WAVE_FORMAT_IEEE_FLOAT;format.nChannels=2;format.nSamplesPerSec=48000;format.wBitsPerSample=32;format.nBlockAlign=8;format.nAvgBytesPerSec=384000;
        hr=client->Initialize(AUDCLNT_SHAREMODE_SHARED,AUDCLNT_STREAMFLAGS_LOOPBACK|AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM|AUDCLNT_STREAMFLAGS_EVENTCALLBACK,0,0,&format,nullptr);
        if(SUCCEEDED(hr)){packets=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!packets)hr=HRESULT_FROM_WIN32(GetLastError());}
        if(SUCCEEDED(hr))hr=client->SetEventHandle(packets);
        if(SUCCEEDED(hr))hr=client->GetService(IID_PPV_ARGS(&capture));
        if(SUCCEEDED(hr))hr=client->Start();
        if(FAILED(hr)){reset();return hr;}
        process=pid;return S_OK;
    }
    HRESULT read(float& peak) {
        peak=0;spectrum.beginBlock();if(!capture)return E_UNEXPECTED;
        UINT32 count=0;HRESULT hr=capture->GetNextPacketSize(&count);
        while(SUCCEEDED(hr)&&count){
            BYTE* data=nullptr;UINT32 frames=0;DWORD flags=0;
            hr=capture->GetBuffer(&data,&frames,&flags,nullptr,nullptr);if(FAILED(hr))return hr;
            if(flags&AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY)spectrum.reset();
            auto pcm=reinterpret_cast<const float*>(data);
            for(size_t i=0;i<frames;i++){
                float left=0,right=0;
                if(pcm&&!(flags&AUDCLNT_BUFFERFLAGS_SILENT)){
                    if(std::isfinite(pcm[i*2]))left=pcm[i*2];
                    if(std::isfinite(pcm[i*2+1]))right=pcm[i*2+1];
                }
                peak=std::max({peak,std::abs(left),std::abs(right)});spectrum.push(left,right);
            }
            hr=capture->ReleaseBuffer(frames);if(FAILED(hr))return hr;
            hr=capture->GetNextPacketSize(&count);
        }
        if(peak==0)spectrum.reset();
        return hr;
    }
};
static DWORD sourceRoot(DWORD pid,const std::wstring& source) {
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(snapshot==INVALID_HANDLE_VALUE)return pid;
    std::vector<std::pair<DWORD,DWORD>> parents;PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);
    if(Process32FirstW(snapshot,&entry))do{parents.emplace_back(entry.th32ProcessID,entry.th32ParentProcessID);}while(Process32NextW(snapshot,&entry));CloseHandle(snapshot);
    for(int i=0;i<32;i++){auto found=std::find_if(parents.begin(),parents.end(),[&](auto pair){return pair.first==pid;});if(found==parents.end()||!found->second||found->second==pid||!sourceMatches(source,found->second))break;pid=found->second;}
    return pid;
}
static std::vector<SessionMeter> findSessions(IMMDeviceEnumerator* enumerator,const std::wstring& source) {
    std::vector<SessionMeter> result;ComPtr<IMMDeviceCollection> devices;
    if(FAILED(enumerator->EnumAudioEndpoints(eRender,DEVICE_STATE_ACTIVE,&devices)))return result;
    UINT count=0;devices->GetCount(&count);
    for(UINT d=0;d<count;d++) {
        ComPtr<IMMDevice> device;ComPtr<IAudioSessionManager2> manager;ComPtr<IAudioSessionEnumerator> sessions;ComPtr<IAudioEndpointVolume> endpoint;
        if(FAILED(devices->Item(d,&device))||FAILED(device->Activate(__uuidof(IAudioSessionManager2),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(manager.GetAddressOf())))||FAILED(manager->GetSessionEnumerator(&sessions)))continue;
        if(FAILED(device->Activate(__uuidof(IAudioEndpointVolume),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(endpoint.GetAddressOf()))))continue;
        int size=0;sessions->GetCount(&size);
        for(int i=0;i<size;i++) {
            ComPtr<IAudioSessionControl> control;ComPtr<IAudioSessionControl2> detail;SessionMeter value;
            if(SUCCEEDED(sessions->GetSession(i,&control))&&SUCCEEDED(control.As(&detail))&&SUCCEEDED(detail->GetProcessId(&value.pid))&&value.pid&&sourceMatches(source,value.pid)&&SUCCEEDED(control.As(&value.meter))&&SUCCEEDED(control.As(&value.volume))){value.endpoint=endpoint;result.push_back(value);}
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
        PeakHistory history;unsigned long long samples=0;std::vector<SessionMeter> sessions;std::wstring source;double refresh=0,retry=0;DWORD loopbackTarget=0,meterPid=0,cachedRoot=0;ProcessLoopback loopback;
        while(true) {
            std::wstring selected;bool active;
            {std::unique_lock lock(mutex);wake.wait_for(lock,std::chrono::milliseconds(25),[&]{return stopping;});if(stopping)break;selected=requested;active=playing;}
            double now=clockSeconds();AudioFrame state;
            if(selected!=source){source=selected;history.reset();samples=0;sessions.clear();refresh=retry=0;loopbackTarget=meterPid=cachedRoot=0;loopback.reset();}
            if(active&&!source.empty()&&enumerator) {
                if(now>=refresh){refresh=now+1;sessions=findSessions(enumerator.Get(),source);meterPid=0;}
                double best=-1;
                for(auto& s:sessions){
                    float peak=0,db=0,sessionVolume=0;BOOL endpointMuted=FALSE,sessionMuted=FALSE;
                    if(FAILED(s.meter->GetPeakValue(&peak))||!std::isfinite(peak)||FAILED(s.endpoint->GetMasterVolumeLevel(&db))||!std::isfinite(db)||FAILED(s.endpoint->GetMute(&endpointMuted))||FAILED(s.volume->GetMute(&sessionMuted))||FAILED(s.volume->GetMasterVolume(&sessionVolume))||!std::isfinite(sessionVolume))continue;
                    float gain=audibleSessionGain(db,sessionVolume,endpointMuted||sessionMuted);
                    double audible=std::clamp(peak,0.f,1.f)*gain;
                    state.available=true;
                    if(audible>best||(audible==best&&peak>state.rawPeak)){best=audible;state.process=s.pid;state.outputGain=gain;state.rawPeak=peak;state.sessionVolume=sessionVolume;state.endpointDb=db;state.endpointMuted=endpointMuted!=FALSE;state.peak=audible;}
                }
                state.matches=static_cast<unsigned>(sessions.size());
                if(state.available){
                    if(state.process!=meterPid){meterPid=state.process;cachedRoot=sourceRoot(state.process,source);}
                    DWORD target=cachedRoot;
                    if(target!=loopbackTarget){loopbackTarget=target;loopback.reset();retry=0;history.reset();}
                    if(!loopback.process&&now>=retry){state.loopbackError=loopback.open(target);retry=now+2;}
                    if(loopback.process){
                        state.loopbackError=loopback.read(state.capturedPeak);
                        if(SUCCEEDED(state.loopbackError)){
                            state.outputMeasured=true;state.outputGain=gainFromDecibels(state.endpointDb,state.endpointMuted);
                            // Loopback already includes Spotify's per-stream and session
                            // gains. Only endpoint dB attenuation remains to be applied.
                            state.peak=state.capturedPeak*state.outputGain;
                        }else{loopback.reset();state.available=false;state.peak=0;retry=now+2;}
                    }
                }else loopback.reset();
                // Older Windows versions without process-loopback retain their basic
                // per-player meter. Modern Windows uses the verified output stream.
                history.append(static_cast<float>(state.peak),state.available);samples++;
                state.peaks=state.outputMeasured?loopback.bands(state.outputGain):history.display();state.samples=samples;
            }else {history.reset();samples=0;sessions.clear();refresh=retry=0;loopbackTarget=meterPid=cachedRoot=0;loopback.reset();}
            state.source=source;
            state.received=clockSeconds();{std::lock_guard lock(mutex);latest=state;}
        }
    }
    CoUninitialize();
}
}
