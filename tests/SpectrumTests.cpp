#include "../src/Spectrum.h"
#include <iostream>
#include <stdexcept>

using namespace island;
static unsigned checks=0;
static void require(bool condition,const char* message){++checks;if(!condition)throw std::runtime_error(message);}
static SpectrumAnalyzer tone(double frequency,float amplitude,bool opposite=false) {
    SpectrumAnalyzer analyzer;
    for(int frame=0;frame<15600;frame++){
        if(frame==14400)analyzer.beginBlock();
        float value=amplitude*static_cast<float>(std::sin(frame*6.283185307179586*frequency/48000));
        analyzer.push(value,opposite?-value:value);
    }
    return analyzer;
}
int main() {
    try {
        constexpr double frequencies[]{75,220,600,1700,5000,12000};
        constexpr size_t barForBand[]{2,3,1,4,0,5};
        for(size_t band=0;band<6;band++) {
            auto analyzer=tone(frequencies[band],.2f);auto energy=analyzer.amplitudes();
            require(energy[band]>.15f,"each instrument range must produce a clearly measured amplitude");
            for(size_t other=0;other<6;other++)if(other!=band)require(energy[band]>energy[other]*2,"bass, mids and treble must drive their own frequency band rather than one shared peak");
            auto display=analyzer.display(1);
            require(display[barForBand[band]+3]>.7f,"each frequency band must reach its assigned visible stroke");
            auto quiet=analyzer.display(.1f);
            require(quiet[barForBand[band]+3]<display[barForBand[band]+3]-.15f,"volume attenuation must remain visible in every frequency band");
            for(float value:analyzer.display(0))require(value==0,"endpoint mute must silence every band immediately");
            auto opposite=tone(frequencies[band],.2f,true).amplitudes();
            for(size_t i=0;i<6;i++)require(std::abs(opposite[i]-energy[i])<.000001f,"out-of-phase stereo must not cancel instruments out of the visualizer");
        }
        SpectrumAnalyzer mix;
        for(int frame=0;frame<15600;frame++) {
            if(frame==14400)mix.beginBlock();
            mix.push(.12f*static_cast<float>(std::sin(frame*6.283185307179586*75/48000)),.12f*static_cast<float>(std::sin(frame*6.283185307179586*12000/48000)));
        }
        auto mixed=mix.amplitudes();require(mixed[0]>.07f&&mixed[5]>.07f,"bass and treble in separate channels must animate simultaneously");
        auto steady=tone(75,.15f);auto before=steady.display(1);steady.beginBlock();
        for(int frame=0;frame<1200;frame++)steady.push(.15f*static_cast<float>(std::sin(frame*6.283185307179586*12000/48000)),.15f*static_cast<float>(std::sin(frame*6.283185307179586*12000/48000)));
        auto after=steady.display(1);
        std::cout<<"same-level bass to treble: bass="<<before[5]<<" -> "<<after[5]<<", treble="<<before[8]<<" -> "<<after[8]<<'\n';
        require(after[8]>before[8]+.4f&&after[5]<before[5]-.15f,"changing instrumentation at the same overall amplitude must visibly change the shape within one audio update");
        SpectrumAnalyzer quiet;quiet.beginBlock();for(int frame=0;frame<1200;frame++)quiet.push(0,0);
        for(float value:quiet.display(1))require(value==0,"silence must never generate motion");
        quiet.push(NAN,INFINITY);for(float value:quiet.display(1))require(std::isfinite(value)&&value==0,"invalid PCM must be safely treated as silence");
        for(int frame=0;frame<1200;frame++)quiet.push(.5f,.5f);quiet.reset();
        for(float value:quiet.display(1))require(value==0,"changing the player must clear old filter energy");
        auto loud=tone(600,.2f),soft=tone(600,.02f);
        require(std::abs(soft.amplitudes()[2]*10-loud.amplitudes()[2])<.000001f,"band measurement must remain proportional to real audio rather than adaptive volume normalization");
        require(soft.display(.12f)[4]>.4f,"quiet instruments must still produce visible movement at normal listening attenuation");
        auto listening=tone(75,.05f).display(gainFromDecibels(-19.279f));
        require(listening[5]>.55f&&listening[5]<.85f&&waveformLayout(0).height(listening[5])>16,"the measured Spotify/endpoint listening range must produce substantial bass movement with headroom");
        SpectrumAnalyzer beats;float animatedLow=1,animatedHigh=0,plainLow=1,plainHigh=0;
        for(int block=0;block<100;block++){
            beats.beginBlock();
            for(int sample=0;sample<1200;sample++){
                double time=(block*1200+sample)/48000.;
                float amplitude=static_cast<float>(.024+.018*std::sin(time*6.283185307179586*2));
                float value=amplitude*static_cast<float>(std::sin(time*6.283185307179586*75));beats.push(value,value);
            }
            if(block>=40){
                float animated=beats.display(.108655f)[5];
                float plain=waveformLevel(beats.amplitudes()[0]*.108655f*4);
                animatedLow=std::min(animatedLow,animated);animatedHigh=std::max(animatedHigh,animated);
                plainLow=std::min(plainLow,plain);plainHigh=std::max(plainHigh,plain);
            }
        }
        std::cout<<"quiet bass beat excursion: plain="<<plainHigh-plainLow<<", reactive="<<animatedHigh-animatedLow<<'\n';
        require(animatedHigh-animatedLow>(plainHigh-plainLow)*1.35f,"quiet rhythmic bass must have clearly greater excursion than sensitivity gain alone");
        for(float value:tone(75,.00001f).display(.108655f))require(value==0,"the sensitivity boost must not turn inaudible floor noise into movement");
        std::cout<<"PASS: "<<checks<<" frequency isolation, same-level instrument changes, bass/treble mixing, stereo phase, volume, quiet music, silence and reset checks\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
