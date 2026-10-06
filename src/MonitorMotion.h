#pragma once
#include "Motion.h"

namespace island {
class MonitorDrag {
    double startX=0,startY=0,originCenter=0,originTop=0,unit=1;
    double dx=0,dy=0;
    Spring detached{0,18,1};
    bool eligible=false,dragging=false;
public:
    double center=0,top=0;
    static double resistance(double delta,double limit){return std::copysign(limit*(1-std::exp(-std::abs(delta)/limit*.45)),delta);}
    void begin(double x,double y,double windowCenter,double windowTop,double scale,bool header){startX=x;startY=y;originCenter=windowCenter;originTop=windowTop;unit=scale;eligible=header;dragging=false;center=windowCenter;top=windowTop;dx=dy=0;detached.snap(0);}
    bool move(double x,double y,bool anotherMonitor=false){
        dx=x-startX;dy=y-startY;
        // Upward vertical pulls retain the hide gesture. Once a transfer has
        // started, changing direction keeps ownership until release.
        if(eligible&&!dragging&&std::hypot(dx,dy)>10*unit&&(dy>=0||std::abs(dx)>std::abs(dy)*.85))dragging=true;
        if(dragging){detached.target=anotherMonitor?1:0;update();}
        return dragging;
    }
    void update(){double free=clamp01(detached.value);center=originCenter+mix(resistance(dx,72*unit),dx,free);top=originTop+mix(resistance(dy,36*unit),dy,free);}
    void step(double dt,bool reduced){detached.step(dt,reduced);update();}
    bool active() const {return dragging;}
    double gestureScale() const {return unit;}
    void cancel(){eligible=dragging=false;}
};
struct MonitorMotion {
    Spring center{0,14,.96},top{0,14,.96},scale{1,18,1};
    bool initialized=false;
    void snap(double x,double y,double density){center.snap(x);top.snap(y);scale.snap(density);initialized=true;}
    void follow(double x,double y){center.frequency=top.frequency=42;center.damping=top.damping=1;center.target=x;top.target=y;}
    void dock(double x,double y,double density){center.frequency=top.frequency=14;center.damping=top.damping=.96;center.target=x;top.target=y;scale.target=density;}
    void step(double dt,bool reduced){center.step(dt,reduced);top.step(dt,reduced);scale.step(dt,reduced);}
    bool moving() const {return center.moving()||top.moving()||scale.moving();}
};
}
