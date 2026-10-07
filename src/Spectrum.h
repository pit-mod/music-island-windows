#pragma once
#include "Waveform.h"

namespace island {
// A streaming stereo filter bank: no averaging the channels together (which
// would lose out-of-phase bass), no volume normalization, and no fake motion.
class SpectrumAnalyzer {
    struct Filter {
        double b0=0,b1=0,b2=0,a1=0,a2=0,z1=0,z2=0;
        void configure(double frequency,bool highpass) {
            constexpr double Pi=3.14159265358979323846;
            double w=2*Pi*frequency/48000,cosine=std::cos(w),alpha=std::sin(w)/std::sqrt(2.0),divisor=1+alpha;
            b0=(highpass?1+cosine:1-cosine)/(2*divisor);
            b1=(highpass?-(1+cosine):1-cosine)/divisor;b2=b0;
            a1=-2*cosine/divisor;a2=(1-alpha)/divisor;
        }
        double process(double input) {
            double output=b0*input+z1;z1=b1*input-a1*output+z2;z2=b2*input-a2*output;
            if(std::abs(z1)<1e-20)z1=0;if(std::abs(z2)<1e-20)z2=0;
            return output;
        }
        void reset(){z1=z2=0;}
    };
    struct Band {
        std::array<Filter,2> high,low;
        double energy=0,peak=0;
    };
    std::array<Band,6> bands{};
    size_t frames=0;
    std::array<float,6> latest{};
    bool hasWindow=false;
    std::array<float,6> trend{};
    bool trendReady=false;
    void followTrend(const std::array<float,6>& levels,double seconds) {
        if(!trendReady){trend=levels;trendReady=true;return;}
        float follow=static_cast<float>(1-std::exp(-seconds*6));
        for(size_t i=0;i<trend.size();i++)trend[i]+=(levels[i]-trend[i])*follow;
    }
    void clearWindow(){frames=0;for(auto& band:bands){band.energy=band.peak=0;}}
    std::array<float,6> windowAmplitudes() const {
        std::array<float,6> result{};
        if(frames)for(size_t i=0;i<bands.size();i++){
            // RMS tracks sustained instruments; a little peak energy keeps drum
            // attacks crisp. Both remain proportional to the actual output.
            result[i]=static_cast<float>(.7*std::sqrt(bands[i].energy/frames)+.3*bands[i].peak);
        }
        return result;
    }
public:
    SpectrumAnalyzer() {
        constexpr double edges[]{20,130,350,1000,3000,8000,20000};
        for(size_t i=0;i<bands.size();i++)for(size_t channel=0;channel<2;channel++){
            bands[i].high[channel].configure(edges[i],true);
            bands[i].low[channel].configure(edges[i+1],false);
        }
    }
    void beginBlock(){clearWindow();latest.fill(0);hasWindow=false;}
    void reset(){beginBlock();trend.fill(0);trendReady=false;for(auto& band:bands)for(size_t channel=0;channel<2;channel++){band.high[channel].reset();band.low[channel].reset();}}
    void push(float left,float right) {
        double samples[]{std::isfinite(left)?std::clamp(left,-1.f,1.f):0,std::isfinite(right)?std::clamp(right,-1.f,1.f):0};
        for(auto& band:bands)for(size_t channel=0;channel<2;channel++){
            double value=band.low[channel].process(band.high[channel].process(samples[channel]));
            band.energy+=value*value;band.peak=std::max(band.peak,std::abs(value));
        }
        frames++;
        // Use the newest complete 10 ms window rather than holding a drum's
        // peak over the entire capture backlog. Short packets use their own RMS.
        if(frames==480){latest=windowAmplitudes();followTrend(latest,.01);hasWindow=true;clearWindow();}
    }
    std::array<float,6> amplitudes() const {
        return hasWindow?latest:windowAmplitudes();
    }
    AudioPeaks display(float outputGain) const {
        AudioPeaks result{};auto levels=amplitudes();
        // Bass in the center, vocals/instruments beside it, treble on the edges.
        constexpr size_t order[]{4,2,0,1,3,5};
        constexpr float sensitivity[]{1,1.1f,1.25f,1.4f,1.6f,1.8f};
        for(size_t i=0;i<6;i++){
            size_t band=order[i];
            // Expand real rises and dips around each band's recent energy. This
            // is additive contrast, never division by a moving volume estimate:
            // a steady quiet song stays quiet, and zero output always stays zero.
            float amplitude=std::max(0.f,levels[band]+(trendReady?1.5f*(levels[band]-trend[band]):0));
            // A fixed +12 dB display gain makes ordinary listening levels visible.
            result[i+3]=waveformLevel(amplitude*outputGain*4*sensitivity[band]);
        }
        return result;
    }
};
}
