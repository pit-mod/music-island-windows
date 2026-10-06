#pragma once
#include <string>
#include <algorithm>
#include <cmath>
namespace island {
class HideGesture {
    double startX=0,startY=0,dx=0,dy=0;
    bool eligible=false,moved=false,pulled=false;
public:
    enum class Result {Click,Cancel,Hide};
    void begin(double x,double y,bool fromHeader){startX=x;startY=y;dx=dy=0;eligible=fromHeader;moved=pulled=false;}
    void move(double x,double y){dx=x-startX;dy=y-startY;moved|=std::hypot(dx,dy)>6;pulled|=eligible&&-dy>6&&-dy>std::abs(dx)*1.2;}
    double amount() const {return pulled?std::clamp(-dy/32.,0.,1.):0;}
    Result finish(double x,double y){move(x,y);return eligible&&-dy>=16&&-dy>std::abs(dx)*1.2?Result::Hide:moved?Result::Cancel:Result::Click;}
    void cancel(){eligible=moved=pulled=false;dx=dy=0;}
};
class EdgeGesture {
    double startX=0,startY=0;
    bool held=false,revealed=false;
public:
    bool active() const {return held;}
    void begin(double x,double y){startX=x;startY=y;held=true;revealed=false;}
    bool move(double x,double y){double dx=x-startX,dy=y-startY;if(!revealed&&dy>=12&&dy>std::abs(dx)*1.2){revealed=true;return true;}return false;}
    bool finish(double x,double y){bool restore=!revealed&&(move(x,y)||std::hypot(x-startX,y-startY)<=6);held=false;return restore;}
};
// A control gesture owns its visual state until the morph finishes. Media
// acknowledgements can arrive late, fail, or briefly report the preceding state.
class PlaybackFeedback {
    bool pending=false,desired=false;
    double started=0;
    std::wstring session,track;
public:
    void request(bool playing,double now,const std::wstring& source,const std::wstring& song) {
        pending=true;desired=playing;started=now;session=source;track=song;
    }
    bool value(double now,bool actual,const std::wstring& source,const std::wstring& song) {
        if(pending&&(session!=source||track!=song||now-started>=3.5||(now-started>=.5&&actual==desired)))pending=false;
        return pending?desired:actual;
    }
};
// Hover is temporary; an intentional click stays open until the user closes it.
// Closing suppresses hover reopening until the pointer has left the island.
class ExpansionController {
public:
    enum class Mode {Compact,Hover,Clicked};
private:
    Mode mode=Mode::Compact;
    double entered=-1,left=-1;
    bool suppressed=false;
public:
    void open(){mode=Mode::Clicked;suppressed=false;}
    void close(){mode=Mode::Compact;suppressed=true;entered=left=-1;}
    void toggle(){if(mode==Mode::Clicked)close();else open();}
    bool clicked() const {return mode==Mode::Clicked;}
    bool update(double now,bool over,bool held,bool pinned,bool hoverEnabled=false) {
        if(!hoverEnabled&&mode==Mode::Hover&&!held)mode=Mode::Compact;
        if(over) {
            left=-1;if(entered<0)entered=now;
            if(hoverEnabled&&!suppressed&&mode==Mode::Compact&&now-entered>=.18)mode=Mode::Hover;
        } else {
            entered=-1;suppressed=false;if(left<0)left=now;
            if(!held&&mode==Mode::Hover&&now-left>=.75)mode=Mode::Compact;
        }
        return pinned||held||mode!=Mode::Compact;
    }
};
}
