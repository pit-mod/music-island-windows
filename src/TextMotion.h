#pragma once
#include "Motion.h"
namespace island {
class Marquee {
    Spring offset{0,18,1};
    double destination=0,waitUntil=0;
    int direction=1;
public:
    double value() const {return offset.value;}
    void restart(double now,bool clear=false){waitUntil=now+2;direction=1;if(clear)offset.snap(0);destination=offset.target=offset.value;}
    double update(double now,double dt,double overflow,bool geometryMoving) {
        overflow=std::max(0.0,overflow);
        if(geometryMoving){
            // Freeze at the current glyph; expansion must not rewind a long title.
            destination=offset.target=offset.value;offset.velocity=0;waitUntil=now+.4;
            return std::max(0.0,offset.value);
        }
        if(overflow<1){destination=0;direction=1;}
        else {
            destination=std::clamp(destination,0.0,overflow);
            if(now>=waitUntil){
                destination=std::clamp(destination+direction*18*std::clamp(dt,0.0,.1),0.0,overflow);
                if(destination>=overflow){direction=-1;waitUntil=now+1.25;}
                else if(destination<=0){direction=1;waitUntil=now+1.25;}
            }
        }
        offset.target=destination;
        offset.step(dt);return std::max(0.0,offset.value);
    }
};
}
