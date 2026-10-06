#pragma once
#include "Waveform.h"
#include <windows.h>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

namespace island {
struct AudioFrame {
    AudioPeaks peaks{};
    double received=0,peak=0;
    bool available=false;
    DWORD process=0;
    unsigned matches=0;
    unsigned long long samples=0;
    std::wstring source;
};
class AudioVisualizer {
    std::mutex mutex;
    std::condition_variable wake;
    bool stopping=false,playing=false;
    std::wstring requested;
    AudioFrame latest;
    std::thread worker;
    void run();
public:
    AudioVisualizer():worker([this]{run();}){}
    ~AudioVisualizer();
    void select(const std::wstring& source,bool active);
    AudioFrame read();
};
}
