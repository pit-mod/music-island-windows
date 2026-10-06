#pragma once
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "Waveform.h"

namespace island {
constexpr UINT MediaMessage=WM_APP+1;
double clockSeconds();
struct Snapshot {
    std::wstring session,track,source,title,artist,error;
    bool available=false,playing=false,play=false,pause=false,previous=false,next=false,seek=false;
    double position=0,duration=0,rate=1,received=0,peak=0;
    AudioPeaks peaks{};
    double audioReceived=0;
    bool audioAvailable=false;
    DWORD audioPid=0;
    unsigned audioMatches=0;
    unsigned long long audioSamples=0;
    std::wstring audioSource;
    unsigned long long artHash=0,revision=0;
    std::shared_ptr<const std::vector<unsigned char>> artwork;
    double elapsed(double now) const {double p=position+(playing?std::max(0.0,now-received)*rate:0);return std::clamp(p,0.0,duration>0?duration:1e10);}
};
enum class Action { Play,Pause,Previous,Next,Seek };
struct Command {Action action;std::wstring session,track;double position=0;};
class MediaService {
    HWND window=nullptr;
    std::mutex mutex;
    std::condition_variable wake;
    bool stopping=false;
    std::deque<Command> commands;
    std::shared_ptr<const Snapshot> latest=std::make_shared<Snapshot>();
    std::thread worker;
    void run();
public:
    explicit MediaService(HWND hwnd):window(hwnd),worker([this]{run();}){}
    ~MediaService();
    std::shared_ptr<const Snapshot> snapshot();
    bool command(Action action,const Snapshot& target,double position=0);
};
}
