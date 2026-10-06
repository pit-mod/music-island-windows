#pragma once
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <string>

namespace island {
inline double clamp01(double x) { return std::clamp(x, 0.0, 1.0); }
inline double smooth(double x) { x=clamp01(x); return x*x*x*(x*(x*6-15)+10); }
inline double mix(double a,double b,double t) { return a+(b-a)*t; }

// Exact damped oscillator. Retargeting preserves position AND velocity, including mid-flight reversals.
struct Spring {
    double value=0,velocity=0,target=0,frequency=21,damping=.83;
    explicit Spring(double v=0,double w=21,double z=.83):value(v),target(v),frequency(w),damping(z){}
    void snap(double v) { value=target=v;velocity=0; }
    void step(double dt,bool reduced=false) {
        dt=std::clamp(dt,0.0,.1);
        double w=reduced?32:frequency,z=reduced?1:damping,x=value-target;
        if(z>=.999) {
            double b=velocity+w*x,e=std::exp(-w*dt);
            value=target+(x+b*dt)*e;velocity=(velocity-w*b*dt)*e;
        } else {
            double a=z*w,f=w*std::sqrt(1-z*z),c=std::cos(f*dt),s=std::sin(f*dt),b=(velocity+a*x)/f,e=std::exp(-a*dt);
            value=target+e*(x*c+b*s);velocity=e*((b*f-a*x)*c-(x*f+a*b)*s);
        }
        if(std::abs(value-target)<.0005&&std::abs(velocity)<.005) snap(target);
    }
    bool moving() const { return std::abs(value-target)>.0005||std::abs(velocity)>.005; }
};
struct SkipCycle {
    double started=-1;
    bool queued=false;
    static constexpr double duration=.42;
    double phase(double now) {
        if(started<0) return 1;
        double age=std::max(0.0,now-started);
        if(age>=duration&&queued) {started+=duration;queued=false;age=std::max(0.0,now-started);}
        return clamp01(age/duration);
    }
    void trigger(double now) { if(phase(now)<1) queued=true;else {started=now;queued=false;} }
};
struct Presence {
    Spring progress{0,14,1};
    void request(bool visible){progress.target=visible?1:0;}
    void snap(bool visible){progress.snap(visible?1:0);}
    void step(double dt,bool reduced){progress.step(dt,reduced);}
    double growth() const {return smooth((progress.value-.18)/.82);}
    double opacity() const {return smooth(progress.value/.18);}
    double content() const {return smooth((progress.value-.45)/.55);}
    bool hidden() const {return progress.target==0&&!progress.moving()&&progress.value==0;}
    struct Bounds {double width,height,radius,top;};
    Bounds bounds(double width,double height,double radius,double top) const {
        double t=growth(),h=mix(12,height,t);
        return {mix(12,width,t),h,std::min(mix(6,radius,t),h/2),mix(top+12,top,t)};
    }
};
struct SeekBar {double left,width,thickness;};
inline SeekBar seekBar(double left,double width,double held) {
    double t=clamp01(held);return {left+28-2*t,width-56+4*t,6+3*t};
}
class ProgressMotion {
    Spring position{0,30,1};
    std::wstring identity;
    bool initialized=false;
public:
    double update(double fraction,const std::wstring& song,double dt,bool reduced,bool dragging=false) {
        fraction=std::isfinite(fraction)?clamp01(fraction):0;
        position.frequency=dragging?46:30;
        if(!initialized||identity!=song){initialized=true;identity=song;position.snap(fraction);}
        else {position.target=fraction;position.step(dt,reduced);}
        return clamp01(position.value);
    }
};
struct Motion {
    Presence presence;
    Spring tuck{0,22,1};
    Spring peek{0,20,1};
    Spring surfacePress{0,40,1};
    Spring width{204},height{36},radius{18},cover{22},coverX{9},coverY{7};
    Spring reveal{0},playGlyph{0,24,1},playPress{0,30,1},previousPress{0,30,1},nextPress{0,30,1},seek{0,27,1};
    SkipCycle previous,next;
    bool expanded=false,reduced=false;
    void setExpanded(bool value) { expanded=value;width.target=value?364:204;height.target=value?192:36;radius.target=value?44:18;cover.target=value?58:22;coverX.target=value?28:9;coverY.target=value?24:7;reveal.target=value?1:0; }
    double verticalOffset() const {return -38*clamp01(tuck.value);}
    double pressScale() const {return 1-.025*clamp01(surfacePress.value);}
    void step(double dt) { presence.step(dt,reduced);tuck.step(dt,reduced);peek.step(dt,reduced);surfacePress.step(dt,reduced);seek.frequency=seek.target>seek.value?34:22;for(auto* s:{&width,&height,&radius,&cover,&coverX,&coverY,&reveal,&playGlyph,&playPress,&previousPress,&nextPress,&seek}) s->step(dt,reduced); }
    bool moving() const { if(presence.progress.moving()||tuck.moving()||peek.moving()||surfacePress.moving())return true;for(auto* s:{&width,&height,&radius,&cover,&coverX,&coverY,&reveal,&playGlyph,&playPress,&previousPress,&nextPress,&seek}) if(s->moving()) return true;return false; }
};
inline std::wstring timeLabel(double seconds) {
    int value=static_cast<int>(std::max(0.0,seconds));wchar_t text[32];
    if(value>=3600) swprintf_s(text,L"%d:%02d:%02d",value/3600,(value/60)%60,value%60);
    else swprintf_s(text,L"%d:%02d",value/60,value%60);
    return text;
}
}
