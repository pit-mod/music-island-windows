#include "../src/Audio.h"
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace island {double clockSeconds(){return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();}}
using Microsoft::WRL::ComPtr;
static void check(HRESULT value){if(FAILED(value))throw std::runtime_error("Audio API HRESULT: "+std::to_string(value));}
int main() {
    HRESULT initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);if(FAILED(initialized))return 1;
    int exit=0;
    try {
        ComPtr<IMMDeviceEnumerator> devices;ComPtr<IMMDevice> endpoint;ComPtr<IAudioClient> player;ComPtr<IAudioRenderClient> output;
        check(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&devices)));check(devices->GetDefaultAudioEndpoint(eRender,eMultimedia,&endpoint));check(endpoint->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(player.GetAddressOf())));
        WAVEFORMATEX format{};format.wFormatTag=WAVE_FORMAT_PCM;format.nChannels=2;format.nSamplesPerSec=48000;format.wBitsPerSample=16;format.nBlockAlign=4;format.nAvgBytesPerSec=192000;
        check(player->Initialize(AUDCLNT_SHAREMODE_SHARED,AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,2000000,0,&format,nullptr));check(player->GetService(IID_PPV_ARGS(&output)));
        UINT32 capacity=0;check(player->GetBufferSize(&capacity));BYTE* data=nullptr;check(output->GetBuffer(capacity,&data));check(output->ReleaseBuffer(capacity,AUDCLNT_BUFFERFLAGS_SILENT));check(player->Start());
        {
            island::AudioVisualizer visualizer;visualizer.select(L"AudioIntegrationTests.exe",true);island::AudioFrame levels;double started=island::clockSeconds();
            // Exercise actual process discovery, CoreAudio session discovery and history sampling with silent playback.
            // This never changes another application's playback or emits an audible test tone.
            while(island::clockSeconds()-started<5) {
                UINT32 padding=0;check(player->GetCurrentPadding(&padding));if(padding<capacity){check(output->GetBuffer(capacity-padding,&data));check(output->ReleaseBuffer(capacity-padding,AUDCLNT_BUFFERFLAGS_SILENT));}
                Sleep(15);levels=visualizer.read();if(levels.available&&levels.samples>=12)break;
            }
            std::cout<<"matches="<<levels.matches<<" pid="<<levels.process<<" samples="<<levels.samples<<" available="<<levels.available<<'\n';
            if(!levels.available||levels.process!=GetCurrentProcessId()||levels.samples<12)throw std::runtime_error("Native per-player audio meter did not find the silent test stream");
            for(double value:levels.peaks)if(value!=0)throw std::runtime_error("Silent playback must not invent visualizer activity");
            visualizer.select(L"AudioIntegrationTests.exe",false);Sleep(100);levels=visualizer.read();if(levels.available||levels.samples)throw std::runtime_error("Pausing must release the capture and reset audio history");
            std::cout<<"PASS: native process selection, audio sampling, silence and pause cleanup\n";
        }
        player->Stop();
    }catch(std::exception const& e){std::cerr<<e.what()<<'\n';exit=1;}
    CoUninitialize();return exit;
}
