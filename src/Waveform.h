#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
namespace island {
using AudioPeaks=std::array<float,9>;
// Direct port of Nezur's MusicAudio: nine recent peaks and 32 samples of relative dynamics.
class PeakHistory {
    AudioPeaks peaks{};
    std::array<float,32> history{};
    size_t count=0;
public:
    void reset(){peaks.fill(0);history.fill(0);count=0;}
    void append(float peak,bool measured) {
        if(!measured){reset();return;}
        std::move(peaks.begin()+1,peaks.end(),peaks.begin());peaks[8]=std::isfinite(peak)?std::clamp(peak,0.f,1.f):0;
        std::move(history.begin()+1,history.end(),history.begin());history[31]=peaks[8];count=std::min(size_t(32),count+1);
    }
    AudioPeaks display() const {
        float mean=0;for(size_t i=32-count;i<32;i++)mean+=history[i];if(count)mean/=static_cast<float>(count);
        auto sorted=history;std::sort(sorted.begin()+32-count,sorted.end());
        float spread=count<2?.065f:std::max(.065f,sorted[32-count+(count-1)*9/10]-sorted[32-count+(count-1)/10]);
        AudioPeaks result{};
        for(size_t i=0;i<9;i++) {
            float raw=peaks[i];if(raw<=.003f)continue;
            float audible=std::clamp((raw-.003f)/.057f,0.f,1.f),relative=std::clamp(.38f+(raw-mean)/spread*.7f,0.f,.95f);
            result[i]=audible*std::clamp(count<8?raw:raw*.4f+relative*.6f,0.f,.95f);
        }
        return result;
    }
};
// Direct port of MusicWaveform's contour and attack/release response.
class VisualizerMotion {
    static constexpr std::array<float,6> Contour{.50f,.78f,1,.94f,.72f,.44f};
    std::array<float,6> values{};
    std::wstring source;
public:
    static constexpr size_t Bars=6;
    void step(const AudioPeaks& peaks,double dt,bool measured,const std::wstring& selected=L"") {
        if(source!=selected){values.fill(0);source=selected;}
        dt=std::clamp(dt,0.0,.25);
        for(size_t i=0;i<Bars;i++) {
            float peak=std::isfinite(peaks[i+3])?std::clamp(peaks[i+3],0.f,1.f):0;
            float target=measured?std::min(1.f,peak*1.35f)*Contour[i]:0;
            values[i]+=(target-values[i])*static_cast<float>(1-std::exp(-dt*(target>values[i]?42:24)));
            if(values[i]<.0001f)values[i]=0;
        }
    }
    float level(size_t i) const {return values[i];}
};
}
