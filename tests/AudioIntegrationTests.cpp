#include "../src/Audio.h"
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
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
        GUID session;check(CoCreateGuid(&session));
        check(player->Initialize(AUDCLNT_SHAREMODE_SHARED,AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,2000000,0,&format,&session));check(player->GetService(IID_PPV_ARGS(&output)));
        ComPtr<ISimpleAudioVolume> sessionVolume;ComPtr<IAudioEndpointVolume> endpointVolume;
        check(player->GetService(IID_PPV_ARGS(&sessionVolume)));check(endpoint->Activate(__uuidof(IAudioEndpointVolume),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(endpointVolume.GetAddressOf())));
        check(sessionVolume->SetMute(FALSE,nullptr));
        check(sessionVolume->SetMasterVolume(1,nullptr));
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
            if(!levels.available||!levels.outputMeasured||FAILED(levels.loopbackError)||levels.process!=GetCurrentProcessId()||levels.samples<12)throw std::runtime_error("Native per-player audio meter did not find the silent test stream");
            for(double value:levels.peaks)if(value!=0)throw std::runtime_error("Silent playback must not invent visualizer activity");
            float master=0;BOOL muted=FALSE;check(endpointVolume->GetMasterVolumeLevel(&master));check(endpointVolume->GetMute(&muted));
            if(std::abs(levels.outputGain-island::gainFromDecibels(master,muted))>.001f)throw std::runtime_error("Native output gain did not match this stream's output device decibel attenuation/mute");
            // Only mute this test's silent session; leave Windows volume and other players alone.
            check(sessionVolume->SetMute(TRUE,nullptr));Sleep(150);levels=visualizer.read();
            if(!levels.available||!levels.outputMeasured||levels.capturedPeak!=0||levels.peak!=0)throw std::runtime_error("A muted player session must suppress pre-volume meter activity");
            check(sessionVolume->SetMute(FALSE,nullptr));Sleep(150);levels=visualizer.read();
            check(endpointVolume->GetMasterVolumeLevel(&master));check(endpointVolume->GetMute(&muted));
            if(std::abs(levels.outputGain-island::gainFromDecibels(master,muted))>.001f)throw std::runtime_error("Unmuting must restore the current endpoint gain without rediscovering sessions");
            // Reproduce Spotify's session-volume path with real nonzero PCM, kept
            // inaudible by zero session volume or mute throughout this test.
            check(sessionVolume->SetMasterVolume(0,nullptr));
            unsigned long long phase=0;
            auto feedSignal=[&]{UINT32 padding=0;check(player->GetCurrentPadding(&padding));UINT32 frames=capacity-padding;if(!frames)return;check(output->GetBuffer(frames,&data));auto pcm=reinterpret_cast<short*>(data);
                for(UINT32 i=0;i<frames;i++,phase++){short value=static_cast<short>(8192*std::sin(phase*6.283185307179586*440/48000));pcm[i*2]=pcm[i*2+1]=value;}check(output->ReleaseBuffer(frames,0));};
            for(int i=0;i<50;i++){feedSignal();Sleep(15);}levels=visualizer.read();
            std::cout<<"process-output zero-session-volume: rawPeak="<<levels.rawPeak<<" sessionVolume="<<levels.sessionVolume<<" outputGain="<<levels.outputGain<<" capturedPeak="<<levels.capturedPeak<<" outputPeak="<<levels.peak<<'\n';
            if(!levels.available||levels.rawPeak<.24f||levels.sessionVolume!=0||!levels.outputMeasured||levels.capturedPeak!=0||levels.peak!=0)throw std::runtime_error("Zero player volume must suppress a real nonzero pre-volume session meter");
            for(float value:levels.peaks)if(value!=0)throw std::runtime_error("A zero-volume player must settle all waveform history despite nonzero raw peaks");
            check(sessionVolume->SetMute(TRUE,nullptr));check(sessionVolume->SetMasterVolume(.25f,nullptr));
            for(int i=0;i<20;i++){feedSignal();Sleep(15);}levels=visualizer.read();
            if(levels.rawPeak<.24f||std::abs(levels.sessionVolume-.25f)>.001f||!levels.outputMeasured||levels.capturedPeak!=0||levels.peak!=0)throw std::runtime_error("Live session volume changes must be read without session rediscovery, with mute preserved");
            check(player->Stop());check(player->Reset());check(output->GetBuffer(capacity,&data));check(output->ReleaseBuffer(capacity,AUDCLNT_BUFFERFLAGS_SILENT));
            check(sessionVolume->SetMasterVolume(1,nullptr));check(player->Start());check(sessionVolume->SetMute(FALSE,nullptr));Sleep(150);
            visualizer.select(L"AudioIntegrationTests.exe",false);Sleep(100);levels=visualizer.read();if(levels.available||levels.samples)throw std::runtime_error("Pausing must release the capture and reset audio history");
            std::cout<<"PASS: native process loopback, isolation from other players, nonzero pre-volume sampling, zero player volume, endpoint decibels, silence, mute/unmute and pause cleanup\n";
        }
        player->Stop();
    }catch(std::exception const& e){std::cerr<<e.what()<<'\n';exit=1;}
    CoUninitialize();return exit;
}
