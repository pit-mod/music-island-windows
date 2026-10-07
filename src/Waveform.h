#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
namespace island {
using AudioPeaks=std::array<float,9>;
inline float gainFromDecibels(float db,bool muted=false) {
    if(muted||!std::isfinite(db))return 0;
    return std::pow(10.f,std::clamp(db,-160.f,24.f)/20.f);
}
inline float audibleSessionGain(float endpointDb,float sessionVolume,bool muted=false) {
    if(!std::isfinite(sessionVolume))return 0;
    return gainFromDecibels(endpointDb,muted)*std::clamp(sessionVolume,0.f,1.f);
}
inline float waveformLevel(float peak) {
    if(!std::isfinite(peak)||peak<=0)return 0;
    // Fixed output dBFS scale. Adaptive normalization would undo changes from
    // Spotify's own slider; a linear amplitude scale makes normal levels tiny.
    float db=20.f*std::log10(peak);
    float level=std::pow(std::clamp((db+66.f)/66.f,0.f,1.f),.85f);
    // Lift quiet music gently, tapering the boost toward full scale. Keep the
    // same silence floor and a fixed curve so player volume still changes it.
    return level+.5f*level*(1-level)*(1-level);
}
// Nine recent audible peaks keep the mod's trailing six-stroke motion.
class PeakHistory {
    AudioPeaks peaks{};
public:
    void reset(){peaks.fill(0);}
    void append(float peak,bool measured) {
        if(!measured){reset();return;}
        std::move(peaks.begin()+1,peaks.end(),peaks.begin());peaks[8]=std::isfinite(peak)?std::clamp(peak,0.f,1.f):0;
    }
    AudioPeaks display() const {
        AudioPeaks result{};
        for(size_t i=0;i<9;i++)result[i]=waveformLevel(peaks[i]);
        return result;
    }
};
// MusicWaveform's contour with a brisk attack and smooth release.
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
            float target=measured?peak*Contour[i]:0;
            values[i]+=(target-values[i])*static_cast<float>(1-std::exp(-dt*(target>values[i]?52:28)));
            if(values[i]<.0001f)values[i]=0;
        }
    }
    float level(size_t i) const {return values[i];}
};
// Geometry stays in device-independent pixels: expanding the surface must never
// scale its bars down. The original six-stroke proportions grow slightly on open.
struct WaveformLayout {
    float stroke,pitch,restHeight,amplitude,rightInset,centerY;
    float width() const {return 5*pitch+stroke;}
    float height(float level) const {return restHeight+std::clamp(level,0.f,1.f)*amplitude;}
};
inline WaveformLayout waveformLayout(float expansion) {
    float t=std::clamp(expansion,0.f,1.f);t=t*t*(3-2*t);
    return {2.7f+.3f*t,4.6f+.6f*t,2.9f+.5f*t,23.75f+6.25f*t,14+14*t,18+22*t};
}
}
