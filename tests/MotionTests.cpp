#include "../src/Motion.h"
#include "../src/Media.h"
#include "../src/Interaction.h"
#include "../src/TextMotion.h"
#include "../src/Accent.h"
#include "../src/MediaPolicy.h"
#include "../src/ArrowKeys.h"
#include "../src/KeyboardPolicy.h"
#include "../src/MonitorMotion.h"
#include <iostream>
#include <stdexcept>
using namespace island;
static int checks=0;
static void require(bool value,const char* reason){++checks;if(!value)throw std::runtime_error(reason);}
int main(){
    try {
        FocusMetadata ordinary;ordinary.known=true;
        require(classifyFocus(ordinary)==FocusKind::Browsing,"ordinary windows, panes and groups must allow arrows without requiring text patterns");
        auto editable=ordinary;editable.edit=true;require(classifyFocus(editable)==FocusKind::Editing,"a focused text field must keep arrows even before typing starts");
        editable=ordinary;editable.password=true;require(classifyFocus(editable)==FocusKind::Editing,"password fields must keep navigation arrows");
        editable=ordinary;editable.textEdit=true;require(classifyFocus(editable)==FocusKind::Editing,"custom controls with a text-edit pattern must keep arrows");
        auto document=ordinary;document.text=true;document.textKnown=true;document.textReadOnly=true;
        require(classifyFocus(document)==FocusKind::Browsing,"a read-only browser document must allow music controls");
        document.textReadOnly=false;require(classifyFocus(document)==FocusKind::Editing,"contenteditable documents must keep navigation arrows");
        document.textKnown=false;require(classifyFocus(document)==FocusKind::Editing,"an ambiguous text document must retain arrow navigation");
        auto valueControl=ordinary;valueControl.value=true;valueControl.valueKnown=true;valueControl.valueReadOnly=true;require(classifyFocus(valueControl)==FocusKind::Browsing,"read-only values must not disable music controls");
        valueControl.valueReadOnly=false;require(classifyFocus(valueControl)==FocusKind::Editing,"editable value patterns must retain navigation arrows");
        require(classifyFocus({})==FocusKind::Unknown,"failed accessibility queries must never approve stale focus");
        FocusApproval approval{101,202,7,1000,FocusKind::Browsing};
        require(approval.allows(101,202,7,1100),"fresh non-editable focus must enable arrow controls");
        require(!approval.allows(303,202,7,1100)&&!approval.allows(101,404,7,1100)&&!approval.allows(101,202,8,1100),"window, native or accessibility focus changes must invalidate old approval");
        require(!approval.allows(101,202,7,1600),"stale focus approval must not consume keys");
        MonitorDrag transport;transport.begin(220,36,220,2,1,true);
        require(!transport.move(223,38),"pointer jitter must remain a click rather than monitor dragging");
        require(!transport.move(220,12),"upward pulls must retain the hide gesture");
        transport.begin(220,36,220,2,1,true);require(transport.move(280,36),"a horizontal header pull must start the elastic monitor gesture");
        require(transport.center>220&&transport.center<250&&transport.top==2,"an anchored drag must follow only partway toward the pointer");
        transport.move(1220,500);require(transport.center<292&&transport.top<38,"same-monitor pulling must remain softly tethered within a small distance");
        transport.move(1220,500,true);double attached=transport.center;transport.step(1./120,false);
        require(transport.center>attached&&transport.center<1220,"entering another monitor must release the tether smoothly without jumping to the pointer");
        for(int i=0;i<180;i++)transport.step(1./120,false);
        require(std::abs(transport.center-1220)<.01,"the transferred island must catch up with the pointer");
        transport.move(220,36,false);transport.cancel();require(!transport.active(),"cancellation must release monitor gesture ownership");
        transport.begin(220,176,220,2,1,false);require(!transport.move(1400,176,true),"dragging player controls must never relocate the island");
        MonitorMotion flight;flight.snap(960,2,1);flight.follow(1600,400);for(int i=0;i<8;i++)flight.step(1./120,false);
        double beforeFlight=flight.center.value,flightVelocity=flight.center.velocity;flight.dock(-960,82,1.5);
        require(flight.center.value==beforeFlight&&flight.center.velocity==flightVelocity,"dropping onto another screen must preserve position and velocity");
        for(int i=0;i<360;i++){flight.step(1./120,false);require(std::isfinite(flight.center.value)&&std::isfinite(flight.top.value)&&flight.scale.value>=1&&flight.scale.value<=1.5,"monitor transfer across negative coordinates and mixed DPI must remain smooth and finite");}
        require(!flight.moving()&&flight.center.value==-960&&flight.top.value==82&&flight.scale.value==1.5,"monitor transfer must settle exactly at the new top-center anchor and DPI");
        require(arrowAction(VK_LEFT)==ArrowAction::Previous&&arrowAction(VK_RIGHT)==ArrowAction::Next&&arrowAction(VK_UP)==ArrowAction::Playback&&arrowAction(VK_DOWN)==ArrowAction::Expand,"Windows arrows must match the Minecraft mod mapping");
        require(arrowAction(VK_SPACE)==ArrowAction::None,"unrelated keys must remain untouched");
        ArrowKeys arrows;
        for(unsigned key:{VK_LEFT,VK_RIGHT,VK_UP,VK_DOWN}){
            auto press=arrows.event(key,true,true);require(press.consume&&press.trigger,"a permitted arrow press must trigger exactly once");
            for(int repeat=0;repeat<8;repeat++){auto held=arrows.event(key,true,true);require(held.consume&&!held.trigger,"holding an arrow must never repeat song skips or toggles");}
            auto release=arrows.event(key,false,false);require(release.consume&&!release.trigger,"a consumed arrow down must also consume its matching up");
            auto typing=arrows.event(key,true,false);require(!typing.consume&&!typing.trigger,"typing, disabled mode, menus, modifiers, no music and uncertain focus must pass arrows through");
            auto stillTyping=arrows.event(key,true,true);require(!stillTyping.consume&&!stillTyping.trigger,"a held typing arrow must not become a music command after focus changes");
            require(!arrows.event(key,false,true).consume,"a typing arrow release must reach the foreground application");
            auto injected=arrows.event(key,true,true,true);require(!injected.consume&&!injected.trigger,"synthetic keys must not become music controls");
            require(!arrows.event(key,false,true,true).consume,"synthetic releases must pass through too");
        }
        require(arrows.event(VK_RIGHT,true,true).trigger,"a fresh press after typing must rearm");
        require(arrows.event(VK_RIGHT,false,false).consume,"disabling the setting mid-press must finish the owned key pair");
        Spring spring(36);spring.target=192;
        for(int i=0;i<10;i++)spring.step(1.0/120);
        double position=spring.value,velocity=spring.velocity;spring.target=36;
        require(spring.value==position&&spring.velocity==velocity,"retargeting must preserve position and velocity");
        spring.step(.000001);require(std::abs(spring.value-position)<.005,"reversal must not jump");
        for(int i=0;i<300;i++){spring.step(1.0/120);require(std::isfinite(spring.value)&&std::isfinite(spring.velocity),"spring must remain finite");}
        require(std::abs(spring.value-36)<.001,"reversed spring must settle exactly");
        double positions[3]{};int rates[]={30,60,144};
        for(int j=0;j<3;j++){Spring s(36);s.target=192;for(int i=0;i<rates[j];i++)s.step(1.0/rates[j]);positions[j]=s.value;}
        require(std::abs(positions[0]-positions[1])<.001&&std::abs(positions[1]-positions[2])<.001,"motion must be independent of frame rate");
        Motion m;m.setExpanded(true);for(int i=0;i<180;i++)m.step(1.0/120);
        require(std::abs(m.width.value-364)<.001&&std::abs(m.height.value-192)<.001,"expanded geometry must settle");
        m.setExpanded(false);for(int i=0;i<180;i++)m.step(1.0/120);
        require(std::abs(m.width.value-204)<.001&&std::abs(m.height.value-36)<.001,"compact island must remain visible");
        HideGesture hide;hide.begin(220,36,true);hide.move(280,36);require(hide.amount()==0&&hide.finish(280,36)==HideGesture::Result::Cancel,"horizontal drags must neither move nor hide the island");
        hide.begin(220,36,true);hide.move(220,24);require(hide.amount()>0&&hide.finish(220,24)==HideGesture::Result::Cancel,"a short upward pull must preview hiding and then spring back");
        hide.begin(220,36,true);hide.move(221,12);require(hide.finish(221,12)==HideGesture::Result::Hide,"a deliberate upward swipe must hide the island");
        hide.begin(220,150,false);require(hide.finish(220,110)==HideGesture::Result::Cancel,"dragging below the header must not hide the island");
        hide.begin(220,36,true);hide.move(220,14);hide.move(220,36);require(hide.amount()==0&&hide.finish(220,36)==HideGesture::Result::Cancel,"returning an upward pull to its origin must cancel without expanding");
        hide.begin(220,36,true);require(hide.finish(221,37)==HideGesture::Result::Click,"small pointer jitter must remain a normal released click");
        EdgeGesture edge;edge.begin(220,0);require(edge.active()&&edge.finish(221,1)&&!edge.active(),"a click on the top edge must restore the island");
        edge.begin(220,0);require(edge.move(221,20)&&!edge.move(221,24)&&!edge.finish(221,24),"pulling downward from the edge must reveal only once");
        edge.begin(220,0);require(!edge.move(260,1)&&!edge.finish(260,1),"horizontal edge drags must not reveal the island");
        Motion tucked;tucked.presence.snap(true);tucked.presence.request(false);tucked.tuck.target=tucked.peek.target=1;
        for(int i=0;i<180;i++)tucked.step(1./120);
        require(tucked.presence.hidden()&&tucked.peek.value==1&&tucked.verticalOffset()==-38,"manual hiding must tuck the panel away and retain only its subtle handle");
        tucked.surfacePress.target=1;for(int i=0;i<12;i++)tucked.step(1./120);require(tucked.pressScale()<.98,"press feedback must gently compress the island");
        tucked.surfacePress.target=0;for(int i=0;i<120;i++)tucked.step(1./120);require(tucked.pressScale()==1,"released click feedback must return to the exact original size");
        Presence presence;require(presence.hidden()&&presence.opacity()==0,"launch without a song must start fully hidden");
        presence.request(true);double lastWidth=12;
        for(int i=0;i<120;i++){
            presence.step(1./120,false);auto bounds=presence.bounds(204,36,18,18);
            require(bounds.width>=lastWidth&&bounds.width<=204&&bounds.height>=12&&bounds.height<=36,"song arrival must grow smoothly from a dot to the compact island");lastWidth=bounds.width;
            if(i==2)require(bounds.width==12&&bounds.height==12&&presence.content()==0&&presence.opacity()>0,"entrance must first show a clean dot with no text or cover");
        }
        require(presence.progress.value==1&&presence.content()==1,"entrance must finish with fully visible content");
        presence.request(false);for(int i=0;i<12;i++)presence.step(1./120,false);
        double appearance=presence.progress.value,speed=presence.progress.velocity;presence.request(true);
        require(presence.progress.value==appearance&&presence.progress.velocity==speed,"a new song during dismissal must reverse without a jump");
        for(int i=0;i<120;i++)presence.step(1./120,false);presence.request(false);
        for(int i=0;i<180;i++)presence.step(1./120,false);require(presence.hidden()&&presence.opacity()==0,"song removal must finish fully hidden");
        MediaCandidate music{L"Spotify.exe",L"Song",L"Artist",L"Album",L""};music.playing=true;
        require(isMusic(music),"untyped Spotify tracks must remain supported");music.playing=false;music.paused=true;require(isMusic(music),"a paused song must remain available for resuming");
        music.paused=false;require(!isMusic(music),"a stopped music session must disappear");music.playing=true;
        music.kind=MediaKind::Video;require(!isMusic(music),"video metadata must never select the island");music.kind=MediaKind::Music;
        music.source=L"Discord.exe";require(!isMusic(music),"Discord must be ignored even if it claims music playback");
        music.source=L"TikTok_123!App";require(!isMusic(music),"TikTok app sessions must be ignored");
        music.source=L"chrome.exe";music.title=L"A clip - TikTok";require(!isMusic(music),"TikTok browser media must be ignored even when typed as music");
        music.title=L"Song";require(isMusic(music),"explicit browser music must stay supported");music.kind=MediaKind::Unknown;require(!isMusic(music),"unidentified browser clips must not replace a music player");
        music.album=L"YouTube Music";require(isMusic(music),"recognized browser music services must remain supported");
        music.title=L"  \t";require(!isMusic(music),"empty media metadata must not show a blank island");
        m.seek.target=1;for(int i=0;i<120;i++)m.step(1.0/120);double held=m.seek.value;m.seek.target=0;
        require(m.seek.value==held,"slider release must not reset its visible size");
        double previous=held;for(int i=0;i<120;i++){m.step(1.0/120);require(m.seek.value<=previous+.000001,"slider must shrink after release");previous=m.seek.value;}
        require(std::abs(m.seek.value)<.001,"slider must return to rest");
        ProgressMotion progress;require(progress.update(.2,L"song",1./120,false)==.2,"a song must start with its actual progress");
        double progressBefore=progress.update(.8,L"song",1./120,false);require(progressBefore>.2&&progressBefore<.22,"click seeking must begin smoothly instead of jumping across the bar");
        double progressPrevious=progressBefore;for(int i=0;i<120;i++){double current=progress.update(.8,L"song",1./120,false);require(current>=progressPrevious&&current<=.8,"seek movement must remain bounded without overshooting");progressPrevious=current;}
        require(std::abs(progressPrevious-.8)<.0001,"seek feedback must settle on the selected position");
        double reversed=progress.update(.1,L"song",1./120,false);require(reversed<.8&&reversed>.75,"drag reversal must keep the visible progress continuous");
        require(progress.update(.05,L"different song",1./120,false)==.05,"a new song must not inherit the preceding song's seek animation");
        ProgressMotion dragged,clicked;dragged.update(.2,L"song",0,false);clicked.update(.2,L"song",0,false);
        double dragValue=0,clickValue=0;for(int i=0;i<12;i++){dragValue=dragged.update(.8,L"song",1./120,false,true);clickValue=clicked.update(.8,L"song",1./120,false);}
        require(dragValue>clickValue&&dragValue<.8,"drag feedback must follow the pointer more promptly than a click transition");
        require(dragged.update(.8,L"song",0,false)==dragValue,"releasing a drag must preserve the visible position when changing easing speed");
        auto restingBar=seekBar(38,364,0),heldBar=seekBar(38,364,1);require(restingBar.thickness==6&&heldBar.thickness==9&&heldBar.width>restingBar.width,"held slider must grow a little from its larger resting track");
        SkipCycle next,back;next.trigger(0);double phase=next.phase(.15);back.trigger(.15);
        require(next.phase(.15)==phase&&back.phase(.15)==0,"skip buttons must animate independently");
        next.trigger(.15);require(next.phase(.15)==phase,"rapid clicks must not reset visible chevrons");
        require(next.phase(.5)>0&&next.phase(.5)<1,"repeated clicks should continue a queued cycle");
        require(next.phase(1)==1&&back.phase(1)==1,"skip cycles must settle");
        for(int i=0;i<=1000;i++){double x=i/1000.;require(smooth(x)>=0&&smooth(x)<=1,"easing must remain bounded");}
        Spring reduced(0);reduced.target=1;double value=0;for(int i=0;i<120;i++){reduced.step(1.0/120,true);require(reduced.value>=value&&reduced.value<=1,"reduced motion must not overshoot");value=reduced.value;}
        Snapshot s;s.playing=true;s.position=30;s.duration=40;s.received=100;s.rate=1;
        require(s.elapsed(105)==35&&s.elapsed(150)==40,"timeline extrapolation must be duration-bounded");s.playing=false;require(s.elapsed(150)==30,"paused progress must stop");
        require(timeLabel(62)==L"1:02"&&timeLabel(3661)==L"1:01:01","time labels must preserve hours and padded seconds");
        PlaybackFeedback playback;playback.request(false,10,L"spotify",L"song");
        require(!playback.value(10,true,L"spotify",L"song"),"pause feedback must begin before the media response");
        require(!playback.value(10.01,false,L"spotify",L"song")&&!playback.value(10.02,true,L"spotify",L"song"),"early or stale acknowledgements must not interrupt the morph");
        Spring glyph(1,24,1);
        for(int i=1;i<=360;i++){double now=10+i/120.;glyph.target=playback.value(now,true,L"spotify",L"song")?1:0;glyph.step(1./120);require(glyph.target==0,"a delayed or rejected pause must let the visual finish");}
        require(glyph.value==0&&!glyph.moving(),"the pause morph must settle while the song still plays");
        require(playback.value(13.5,true,L"spotify",L"song"),"unacknowledged controls must eventually reconcile to actual playback");
        playback.request(false,20,L"spotify",L"song");double glyphValue=glyph.value,glyphVelocity=glyph.velocity;
        playback.request(true,20.02,L"spotify",L"song");glyph.target=playback.value(20.02,false,L"spotify",L"song")?1:0;
        require(glyph.target==1&&glyph.value==glyphValue&&glyph.velocity==glyphVelocity,"rapid playback reversal must preserve the existing animation");
        require(!playback.value(20.03,false,L"browser",L"song"),"a different player must discard stale local playback feedback");
        playback.request(true,30,L"spotify",L"song");require(!playback.value(30.01,false,L"spotify",L"new song"),"a different song must use its own playback state");
        playback.request(false,40,L"spotify",L"song");require(!playback.value(40.6,false,L"spotify",L"song")&&playback.value(40.7,true,L"spotify",L"song"),"confirmed feedback should follow later external playback changes");
        require(std::abs(gainFromDecibels(-20)-.1f)<.000001f&&std::abs(gainFromDecibels(-6)-.501187f)<.000001f,"endpoint decibels must convert to physical amplitude rather than Windows slider percentage");
        require(gainFromDecibels(0)==1&&gainFromDecibels(-17,true)==0&&gainFromDecibels(NAN)==0,"full output, mute and invalid endpoint readings must have explicit gains");
        require(std::abs(audibleSessionGain(-20,.5f)-.05f)<.000001f,"the player's linear session volume must multiply the endpoint's physical gain");
        require(audibleSessionGain(-17,0)==0&&audibleSessionGain(-17,.5f,true)==0&&audibleSessionGain(-17,NAN)==0,"zero Spotify volume, mute and invalid session gain must suppress pre-volume peaks");
        require(std::abs(audibleSessionGain(0,.1f)-audibleSessionGain(-20,1))<.000001f,"equal player and endpoint attenuation must produce equal output amplitude");
        require(waveformLevel(0)==0&&waveformLevel(.00001f)==0&&waveformLevel(NAN)==0&&waveformLevel(1)==1,"silence, invalid readings and full-scale audio must have stable display endpoints");
        float previousDbLevel=0;for(int db=-96;db<=0;db++){
            float level=waveformLevel(gainFromDecibels(static_cast<float>(db)));
            require(std::isfinite(level)&&level>=previousDbLevel&&level<=1,"the output dB display curve must be finite and monotonic");previousDbLevel=level;
        }
        PeakHistory history;for(int i=0;i<32;i++)history.append(.25f,true);
        auto peaks=history.display();for(float peak:peaks)require(peak>.83f&&peak<.88f,"full-volume music must produce a strong waveform with room for louder transients");
        float lowLevel=waveformLevel(.017334f*gainFromDecibels(-19.279f));
        require(lowLevel>.28f&&lowLevel<.35f,"quiet post-volume music must move the central stroke visibly without looking loud");
        VisualizerMotion quietBeat;AudioPeaks quietBeatPeaks{};quietBeatPeaks.fill(lowLevel);
        quietBeat.step(quietBeatPeaks,1./60,true,L"spotify");quietBeat.step(quietBeatPeaks,1./60,true,L"spotify");
        require(quietBeat.level(2)>lowLevel*.8f&&quietBeat.level(2)<lowLevel,"quiet beats must rise promptly within two display frames without snapping");
        float beatTop=quietBeat.level(2);quietBeatPeaks.fill(0);quietBeat.step(quietBeatPeaks,1./60,true,L"spotify");
        require(quietBeat.level(2)>beatTop*.5f&&quietBeat.level(2)<beatTop*.7f,"quiet beats must release smoothly while retaining distinct movement");
        VisualizerMotion visual;for(int i=0;i<120;i++)visual.step(peaks,1.0/120,true,L"spotify");
        float contour[]={.50f,.78f,1,.94f,.72f,.44f};for(size_t i=0;i<6;i++)require(std::abs(visual.level(i)-peaks[i+3]*contour[i])<.000002f,"all six waveform strokes must retain the original contour");
        float musicLevel=visual.level(2);history.reset();for(int i=0;i<300;i++)history.append(.025f,true);
        auto quietPeaks=history.display();require(quietPeaks[8]<peaks[8]-.2f,"Spotify attenuation must remain visible after adaptive history would have settled");
        visual.step(quietPeaks,1./120,true,L"spotify");require(visual.level(2)<musicLevel&&visual.level(2)>quietPeaks[8],"lowering Spotify volume must smoothly shrink the existing waveform");
        for(int i=0;i<120;i++)visual.step(quietPeaks,1./120,true,L"spotify");
        require(std::abs(visual.level(2)-quietPeaks[8])<.000002f,"a quiet song must never normalize itself back to the full-volume level");
        PeakHistory deviceQuiet;for(int i=0;i<300;i++)deviceQuiet.append(.25f*gainFromDecibels(-20),true);
        require(std::abs(deviceQuiet.display()[8]-quietPeaks[8])<.000002f,"equal attenuation inside Spotify or at the output device must give equal heights without double attenuation");
        PeakHistory listening;for(int i=0;i<32;i++)listening.append(.4f*gainFromDecibels(-17.2615f),true);
        require(listening.display()[8]>.65f&&waveformLayout(0).height(listening.display()[8])>18,"normal listening attenuation must keep the bars clearly visible");
        history.reset();for(int i=0;i<120;i++)visual.step(history.display(),1./120,true,L"spotify");
        require(visual.level(2)==0,"muting must settle the waveform even if the source still produces music");
        visual.step(peaks,1./120,true,L"spotify");require(visual.level(2)>0&&visual.level(2)<peaks[8],"unmuting must resume without snapping to full height");
        auto wavePrevious=waveformLayout(0);
        for(int i=1;i<=240;i++){
            auto wave=waveformLayout(i/240.f);
            require(wave.stroke>=wavePrevious.stroke&&wave.width()>=wavePrevious.width(),"waveform strokes and spacing must never shrink while expanding");
            for(float level:{0.f,.25f,.5f,1.f})require(wave.height(level)>=wavePrevious.height(level),"every waveform height must remain monotonic through expansion");
            require(wave.pitch>wave.stroke&&wave.restHeight>=wave.stroke,"rounded bars must keep clear gaps and valid round caps");
            wavePrevious=wave;
        }
        visual.step(peaks,0,false,L"new player");for(size_t i=0;i<6;i++)require(visual.level(i)==0,"changing the selected player must clear the preceding waveform");
        for(int i=0;i<120;i++)visual.step(peaks,1.0/120,true,L"spotify");for(int i=0;i<60;i++)visual.step(peaks,1.0/120,false,L"spotify");
        for(size_t i=0;i<6;i++)require(visual.level(i)<.0001f,"paused and stale samples must settle every stroke to rest");
        history.reset();history.append(.00001f,true);for(float peak:history.display())require(peak==0,"silence must not invent waveform activity");
        history.append(NAN,true);for(float peak:history.display())require(std::isfinite(peak),"invalid meter readings must stay finite");
        Marquee marquee;marquee.restart(0);double previousScroll=0;
        for(int i=0;i<600;i++){double scrollValue=marquee.update(i/120.0,1.0/120,240,false);require(std::abs(scrollValue-previousScroll)<1,"long titles must scroll continuously");previousScroll=scrollValue;}
        double before=marquee.value();require(before>40,"resize verification must start with a scrolled title");
        for(int i=0;i<120;i++)require(marquee.update(5+i/120.0,1.0/120,90,true)==before,"resizing must preserve the current glyph without rewinding");
        for(int i=0;i<120;i++)marquee.update(6+i/120.0,1.0/120,90,false);require(marquee.value()>before,"scrolling must resume from its preceding position after expansion");
        double narrowBefore=marquee.value();double narrow=marquee.update(7,1.0/120,15,false);require(std::abs(narrow-narrowBefore)<1,"a changed scroll bound must ease without a positional jump");
        for(int i=0;i<120;i++)marquee.update(7+i/120.0,1.0/120,15,false);require(marquee.value()<=15.001,"new scroll bounds must settle within the visible title");
        std::vector<unsigned char> pink(32*32*4);for(size_t i=0;i<pink.size();i+=4){pink[i]=130;pink[i+1]=100;pink[i+2]=230;pink[i+3]=255;}auto palette=ArtworkAccent::columns(pink,32,32);
        for(size_t i=1;i<12;i++)require(palette[i]==palette[0],"solid cover colors must produce the original uniform accent field");
        require(ArtworkAccent::blend(0xffef9a9a,0xffd48aab,0)==0xffef9a9a&&ArtworkAccent::blend(0xffef9a9a,0xffd48aab,1)==0xffd48aab,"palette transition endpoints must remain exact");
        ExpansionController interaction;
        ExpansionController clickOnly;
        require(!clickOnly.update(0,true,false,false)&&!clickOnly.update(60,true,false,false),"hover must never expand with the default click-only setting");
        clickOnly.open();require(clickOnly.update(61,false,false,false),"click-only expansion must still latch a deliberate click");
        require(!interaction.update(0,true,false,false,true),"hover must wait before expanding");
        require(interaction.update(.2,true,false,false,true),"an enabled settled hover should expand");
        interaction.open();require(interaction.clicked()&&interaction.update(10,false,false,false),"clicking an already hovered island must keep it open after leaving");
        interaction.close();require(!interaction.update(11,true,false,false)&&!interaction.update(12,true,false,false),"an explicit close must not immediately reopen under the same pointer");
        interaction.update(13,false,false,false,true);require(!interaction.update(14,true,false,false,true)&&interaction.update(14.2,true,false,false,true),"hover should rearm only after leaving and returning");
        require(interaction.update(15,false,true,false,true)&&interaction.update(16,false,true,false,true),"held controls must prevent hover collapse");
        require(!interaction.update(16.1,false,false,false),"temporary hover should close after release outside");
        require(interaction.update(17,false,false,true),"keep-expanded mode must override automatic collapse");
        interaction.toggle();require(interaction.clicked()&&interaction.update(20,false,false,false),"hotkey opening must latch the expanded state");
        interaction.toggle();require(!interaction.update(21,true,false,false),"hotkey close must suppress immediate hover reopening");
        std::cout<<"PASS: "<<checks<<" native motion, presence, music filtering, waveform/accent, long titles, playback, optional hover/click, capture, skip, seek and timeline assertions\n";return 0;
    }catch(std::exception const& error){std::cerr<<error.what()<<'\n';return 1;}
}
