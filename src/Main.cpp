#include "Renderer.h"
#include "Interaction.h"
#include "ArrowKeys.h"
#include "Keyboard.h"
#include "MonitorMotion.h"
#include <winrt/base.h>
#include <shellapi.h>
#include <shlobj.h>
#include <windowsx.h>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace island {
constexpr UINT TrayMessage=WM_APP+2;
constexpr UINT OutsideMessage=WM_APP+3;
constexpr UINT RevealMessage=WM_APP+4;
constexpr UINT ArrowMessage=WM_APP+5;
class App;
static App* hookedApp=nullptr;
enum class Pointer {None,Surface,Play,Previous,Next,Seek};
struct Settings {
    std::wstring file;
    std::wstring monitorDevice;
    bool pin=false,reduced=false,hover=false,arrows=false;
    double scale=1;
    Settings(){PWSTR directory=nullptr;if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&directory))){std::filesystem::path folder=std::filesystem::path(directory)/L"MusicIslandWindows";CoTaskMemFree(directory);std::filesystem::create_directories(folder);file=(folder/L"settings.ini").wstring();pin=GetPrivateProfileIntW(L"island",L"pin",0,file.c_str())!=0;reduced=GetPrivateProfileIntW(L"island",L"reduced",0,file.c_str())!=0;hover=GetPrivateProfileIntW(L"island",L"hover",0,file.c_str())!=0;arrows=GetPrivateProfileIntW(L"island",L"arrows",0,file.c_str())!=0;wchar_t monitorName[64]{};GetPrivateProfileStringW(L"island",L"monitor",L"",monitorName,64,file.c_str());monitorDevice=monitorName;wchar_t value[32];GetPrivateProfileStringW(L"island",L"scale",L"1",value,32,file.c_str());scale=std::clamp(wcstod(value,nullptr),.75,1.4);}}
    void save(){if(file.empty())return;WritePrivateProfileStringW(L"island",L"pin",pin?L"1":L"0",file.c_str());WritePrivateProfileStringW(L"island",L"reduced",reduced?L"1":L"0",file.c_str());WritePrivateProfileStringW(L"island",L"hover",hover?L"1":L"0",file.c_str());WritePrivateProfileStringW(L"island",L"arrows",arrows?L"1":L"0",file.c_str());WritePrivateProfileStringW(L"island",L"monitor",monitorDevice.c_str(),file.c_str());WritePrivateProfileStringW(L"island",L"scale",std::to_wstring(scale).c_str(),file.c_str());}
};
static std::shared_ptr<const std::vector<unsigned char>> fixtureArt(int variant) {
    Microsoft::WRL::ComPtr<IWICImagingFactory> wic;winrt::check_hresult(CoCreateInstance(CLSID_WICImagingFactory2,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic)));
    Microsoft::WRL::ComPtr<IStream> stream;winrt::check_hresult(CreateStreamOnHGlobal(nullptr,TRUE,&stream));Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;Microsoft::WRL::ComPtr<IPropertyBag2> options;
    winrt::check_hresult(wic->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder));winrt::check_hresult(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));winrt::check_hresult(encoder->CreateNewFrame(&frame,&options));winrt::check_hresult(frame->Initialize(options.Get()));winrt::check_hresult(frame->SetSize(256,256));auto format=GUID_WICPixelFormat32bppBGRA;winrt::check_hresult(frame->SetPixelFormat(&format));
    std::vector<unsigned char> pixels(256*256*4);
    for(int y=0;y<256;y++)for(int x=0;x<256;x++){double distance=std::hypot(x-128.0,y-128.0)/181,ring=std::pow(.5+.5*std::sin(distance*55),8),shade=.8-distance*.5;int index=(y*256+x)*4;pixels[index]=static_cast<unsigned char>(std::clamp((variant?195:55)*shade+ring*48,0.0,255.0));pixels[index+1]=static_cast<unsigned char>(std::clamp(80*shade+ring*55,0.0,255.0));pixels[index+2]=static_cast<unsigned char>(std::clamp((variant?110:235)*shade+ring*65,0.0,255.0));pixels[index+3]=255;}
    winrt::check_hresult(frame->WritePixels(256,256*4,static_cast<UINT>(pixels.size()),pixels.data()));winrt::check_hresult(frame->Commit());winrt::check_hresult(encoder->Commit());STATSTG stat{};stream->Stat(&stat,STATFLAG_NONAME);auto bytes=std::make_shared<std::vector<unsigned char>>(stat.cbSize.LowPart);LARGE_INTEGER zero{};stream->Seek(zero,STREAM_SEEK_SET,nullptr);ULONG read=0;stream->Read(bytes->data(),static_cast<ULONG>(bytes->size()),&read);return bytes;
}
static Snapshot fixture(int variant,double now) {Snapshot s;s.available=s.playing=s.play=s.pause=s.previous=s.next=s.seek=true;s.session=L"preview";s.track=variant?L"two":L"one";s.source=L"MusicIsland preview";s.title=variant?L"After Hours":L"Late Night Drive";s.artist=L"MusicIsland preview";s.position=62;s.duration=214;s.received=now;s.peak=.45;s.artHash=variant?2:1;s.artwork=fixtureArt(variant);return s;}
class App {
public:
    ~App(){if(mouseHook)UnhookWindowsHookEx(mouseHook);if(keyboardHook)UnhookWindowsHookEx(keyboardHook);if(focusHook)UnhookWinEvent(focusHook);if(foregroundHook)UnhookWinEvent(foregroundHook);if(hookedApp==this)hookedApp=nullptr;}
    HWND window=nullptr;
    HHOOK mouseHook=nullptr;
    HHOOK keyboardHook=nullptr;
    HWINEVENTHOOK focusHook=nullptr,foregroundHook=nullptr;
    TypingGuard typing;
    ArrowKeys arrowKeys;
    std::unique_ptr<Renderer> renderer;
    std::unique_ptr<MediaService> media;
    std::shared_ptr<const Snapshot> snapshot=std::make_shared<Snapshot>(),pointerTarget;
    std::shared_ptr<const Snapshot> displaySnapshot=snapshot;
    Settings settings;
    Motion motion;
    ExpansionController expansion;
    PlaybackFeedback playback;
    HideGesture hideGesture;
    EdgeGesture edgeGesture;
    MonitorDrag monitorDrag;
    MonitorMotion placement;
    HMONITOR homeMonitor=nullptr;
    bool manualHidden=false;
    bool preview=false,layoutPending=true,surfaceCloses=false,menuActive=false,shown=false,testing=false;
    float physicalScale=1;
    Pointer pointer=Pointer::None;
    double last=0,skipAt=-10,seekUntil=0,candidate=0,pendingSeek=0,expandAfter=0,pressUntil=0;
    std::wstring seekTrack;
    int direction=0;
    NOTIFYICONDATAW tray{};
    UINT taskbarCreated=0;
    std::wstring exe;
    POINT screenPoint(float x,float y) const {POINT point{static_cast<LONG>(std::lround(x*physicalScale)),static_cast<LONG>(std::lround(y*physicalScale))};ClientToScreen(window,&point);return point;}
    static BOOL CALLBACK findMonitor(HMONITOR monitor,HDC,LPRECT,LPARAM data){auto app=reinterpret_cast<App*>(data);MONITORINFOEXW info{};info.cbSize=sizeof(info);if(GetMonitorInfoW(monitor,&info)&&app->settings.monitorDevice==info.szDevice)app->homeMonitor=monitor;return TRUE;}
    MONITORINFOEXW monitorInfo() {
        MONITORINFOEXW info{};info.cbSize=sizeof(info);
        if(!homeMonitor&&!settings.monitorDevice.empty())EnumDisplayMonitors(nullptr,nullptr,findMonitor,reinterpret_cast<LPARAM>(this));
        if(!homeMonitor||!GetMonitorInfoW(homeMonitor,&info)){homeMonitor=MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST);GetMonitorInfoW(homeMonitor,&info);}
        return info;
    }
    double monitorScale(const MONITORINFOEXW& info) const {
        // A hidden probe receives the selected monitor's DPI without moving or
        // activating the island, including monitors with different scaling.
        HWND probe=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"STATIC",nullptr,WS_POPUP,(info.rcWork.left+info.rcWork.right)/2,info.rcWork.top+2,1,1,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        UINT dpi=probe?GetDpiForWindow(probe):GetDpiForWindow(window);if(probe)DestroyWindow(probe);
        return std::clamp(std::min(dpi/96.*settings.scale,(info.rcWork.right-info.rcWork.left-24)/static_cast<double>(CanvasWidth)),.5,4.);
    }
    void applyPlacement(double dt) {
        if(!placement.initialized)return;
        placement.step(dt,motion.reduced);physicalScale=static_cast<float>(placement.scale.value);
        if(renderer){float reference=renderer->renderingDpi()/96;float required=static_cast<float>(std::max(placement.scale.value,placement.scale.target));if(reference+.001f<required){renderer->resize(96*required);reference=required;}renderer->outputScale(physicalScale/reference);}
        int width=static_cast<int>(std::ceil(CanvasWidth*physicalScale)),height=static_cast<int>(std::ceil(CanvasHeight*physicalScale));
        SetWindowPos(window,HWND_TOPMOST,static_cast<int>(std::lround(placement.center.value-width/2.)),static_cast<int>(std::lround(placement.top.value)),width,height,SWP_NOACTIVATE);
    }
    bool hit(float x,float y) const {
        if(manualHidden||!snapshot->available||motion.presence.opacity()<.005)return false;
        auto bounds=motion.presence.bounds(motion.width.value,motion.height.value,motion.radius.value,BodyTop+motion.verticalOffset());
        double scale=motion.pressScale();bounds.top+=bounds.height*(1-scale)/2;bounds.width*=scale;bounds.height*=scale;bounds.radius*=scale;
        float left=static_cast<float>((CanvasWidth-bounds.width)/2),top=static_cast<float>(bounds.top),w=static_cast<float>(bounds.width),h=static_cast<float>(bounds.height),r=static_cast<float>(bounds.radius);
        float qx=std::abs(x-(left+w/2))-(w/2-r),qy=std::abs(y-(top+h/2))-(h/2-r);
        return std::hypot(std::max(qx,0.f),std::max(qy,0.f))+std::min(std::max(qx,qy),0.f)-r<=0;
    }
    bool effectivePlaying(double now) {return playback.value(now,snapshot->playing,snapshot->session,snapshot->track);}
    void addTray() {tray.cbSize=sizeof(tray);tray.hWnd=window;tray.uID=1;tray.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;tray.uCallbackMessage=TrayMessage;tray.hIcon=LoadIconW(nullptr,IDI_APPLICATION);wcscpy_s(tray.szTip,L"MusicIsland â€” click to expand, right-click for options");Shell_NotifyIconW(NIM_ADD,&tray);tray.uVersion=NOTIFYICON_VERSION_4;Shell_NotifyIconW(NIM_SETVERSION,&tray);}
    void layout() {
        if(monitorDrag.active()){layoutPending=false;return;}
        auto info=monitorInfo();double density=monitorScale(info),center=(info.rcWork.left+info.rcWork.right)/2.,top=info.rcWork.top+2.;
        if(!placement.initialized||!shown)placement.snap(center,top,density);else placement.dock(center,top,density);
        layoutPending=false;applyPlacement(0);
    }
    void region() {
        auto bounds=motion.presence.bounds(motion.width.value,motion.height.value,motion.radius.value,BodyTop+motion.verticalOffset());
        int left=static_cast<int>(((CanvasWidth-bounds.width)/2-7)*physicalScale),right=static_cast<int>(((CanvasWidth+bounds.width)/2+7)*physicalScale),top=static_cast<int>((bounds.top-7)*physicalScale),bottom=static_cast<int>((bounds.top+bounds.height+9)*physicalScale),radius=static_cast<int>((bounds.radius+7)*2*physicalScale);
        HRGN value=CreateRoundRectRgn(left,top,right+1,bottom+1,radius,radius);
        if(motion.peek.value>.001||motion.peek.target>0){HRGN handle=CreateRoundRectRgn(static_cast<int>((CanvasWidth/2-28)*physicalScale),0,static_cast<int>((CanvasWidth/2+28)*physicalScale)+1,static_cast<int>(8*physicalScale)+1,static_cast<int>(6*physicalScale),static_cast<int>(6*physicalScale));CombineRgn(value,value,handle,RGN_OR);DeleteObject(handle);}
        if(!SetWindowRgn(window,value,FALSE))DeleteObject(value);
    }
    bool musicAvailable(double now) const {return snapshot->available&&(preview||now-snapshot->received<6);}
    void hide(){manualHidden=true;expandAfter=pressUntil=0;expansion.close();motion.setExpanded(false);cancel();updatePresence(clockSeconds());}
    void reveal(){if(!musicAvailable(clockSeconds()))return;manualHidden=false;expandAfter=pressUntil=0;expansion.close();motion.setExpanded(settings.pin);updatePresence(clockSeconds());}
    bool edgeHit(POINT point) const {
        MONITORINFO info{sizeof(info)};if(!GetMonitorInfoW(homeMonitor?homeMonitor:MonitorFromWindow(window,MONITOR_DEFAULTTOPRIMARY),&info))return false;
        RECT rect{};GetWindowRect(window,&rect);double center=(rect.left+rect.right)/2.;
        bool screenEdge=point.y>=info.rcMonitor.top&&point.y<info.rcMonitor.top+6*physicalScale;
        bool handle=point.y>=rect.top&&point.y<rect.top+8*physicalScale;
        return std::abs(point.x-center)<=44*physicalScale&&(screenEdge||handle);
    }
    bool edgeMouse(UINT message,POINT point) {
        double x=point.x/physicalScale,y=point.y/physicalScale;
        if(edgeGesture.active()){
            if(message==WM_MOUSEMOVE){if(edgeGesture.move(x,y))PostMessageW(window,RevealMessage,0,0);return false;}
            if(message==WM_LBUTTONUP){if(edgeGesture.finish(x,y))PostMessageW(window,RevealMessage,0,0);return true;}
            if(message==WM_LBUTTONDOWN)return true;
        }
        if(message==WM_LBUTTONDOWN&&(manualHidden||motion.peek.value>.02)&&musicAvailable(clockSeconds())&&!menuActive&&edgeHit(point)){edgeGesture.begin(x,y);return true;}
        return false;
    }
    void toggleExpanded() {if(!snapshot->available)return;if(manualHidden){reveal();return;}expandAfter=0;expansion.toggle();motion.setExpanded(settings.pin||expansion.clicked());}
    bool arrowAvailable(unsigned key) const {
        if(!settings.arrows||!musicAvailable(clockSeconds())||menuActive||pointer!=Pointer::None)return false;
        switch(arrowAction(key)){case ArrowAction::Previous:return snapshot->previous;case ArrowAction::Next:return snapshot->next;case ArrowAction::Playback:return snapshot->play||snapshot->pause;case ArrowAction::Expand:return true;default:return false;}
    }
    void arrow(unsigned key,double now) {
        if(!arrowAvailable(key))return;
        switch(arrowAction(key)){
        case ArrowAction::Previous:send(Action::Previous,now);break;
        case ArrowAction::Next:send(Action::Next,now);break;
        case ArrowAction::Playback:send(effectivePlaying(now)?Action::Pause:Action::Play,now);break;
        case ArrowAction::Expand:toggleExpanded();break;
        default:break;
        }
    }
    void closeFromOutside(){if(!settings.pin&&pointer==Pointer::None&&!menuActive){expansion.close();motion.setExpanded(false);}}
    void updateSnapshot() {
        if(media)snapshot=media->snapshot();
        if(pointerTarget&&(pointerTarget->track!=snapshot->track||pointerTarget->session!=snapshot->session))cancel();
        if(snapshot->track!=seekTrack||!snapshot->error.empty()||std::abs(snapshot->position-pendingSeek)<1.3)seekUntil=0;
        updatePresence(clockSeconds());
    }
    void updatePresence(double now) {
        bool music=musicAvailable(now),visible=music&&!manualHidden;
        if(music)displaySnapshot=snapshot;
        if(!visible&&motion.presence.progress.target!=0){expandAfter=pressUntil=0;cancel();expansion.close();motion.setExpanded(false);}
        double pull=pointer==Pointer::Surface?hideGesture.amount():0;
        motion.presence.progress.target=visible?1-.88*pull:0;
        motion.tuck.target=manualHidden?1:pull;
        motion.peek.target=music&&manualHidden?1:0;
        if((visible||motion.peek.target>0)&&!shown){shown=true;last=now;region();if(!testing)ShowWindow(window,SW_SHOWNOACTIVATE);}
    }
    void updateExpansion(double now,bool over) {
        bool held=pointer!=Pointer::None&&(pointer!=Pointer::Surface||motion.expanded);
        bool expanded=motion.presence.progress.target!=0&&expansion.update(now,over&&pointer!=Pointer::Surface,held,settings.pin,settings.hover);
        motion.setExpanded(expanded&&now>=expandAfter);
        motion.surfacePress.target=(pointer==Pointer::Surface&&hideGesture.amount()==0)||now<pressUntil?1:0;
    }
    bool needsFrames() const {return shown&&(layoutPending||!manualHidden||motion.moving()||placement.moving());}
    void tick(double now,bool present=true) {
        if(layoutPending)layout();double dt=last?std::clamp(now-last,0.0,.1):1.0/60;last=now;
        updatePresence(now);POINT screen{};GetCursorPos(&screen);ScreenToClient(window,&screen);bool over=hit(screen.x/physicalScale,screen.y/physicalScale);
        updateExpansion(now,over);
        BOOL animations=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animations,0);motion.reduced=settings.reduced||!animations;
        bool playing=effectivePlaying(now);motion.playGlyph.target=playing?1:0;
        motion.seek.target=pointer==Pointer::Seek?1:0;motion.playPress.target=pointer==Pointer::Play?1:0;motion.previousPress.target=pointer==Pointer::Previous?1:0;motion.nextPress.target=pointer==Pointer::Next?1:0;
        motion.step(dt);if(monitorDrag.active()){monitorDrag.step(dt,motion.reduced);placement.follow(monitorDrag.center,monitorDrag.top);}if(placement.moving()||monitorDrag.active())applyPlacement(dt);region();double elapsed=pointer==Pointer::Seek?candidate:seekUntil>now?pendingSeek:displaySnapshot->elapsed(now);
        if(preview){auto s=std::make_shared<Snapshot>(*snapshot);s->position=s->elapsed(now);for(size_t i=0;i<9;i++)s->peaks[i]=s->playing?static_cast<float>(.15+.65*std::pow(.5+.5*std::sin(now*(3+i*.37)+i*1.3),2)):0;s->audioSource=s->source;s->received=s->audioReceived=now;snapshot=s;}
        if(preview&&snapshot->available)displaySnapshot=snapshot;
        renderer->render(motion,*displaySnapshot,now,elapsed,playing,preview,now-skipAt<2?direction:0,present);
        if(motion.presence.hidden()&&!motion.peek.moving()&&motion.peek.value==0&&shown){if(!testing)ShowWindow(window,SW_HIDE);shown=false;}
    }
    void press(float x,float y,double /*now*/) {
        if(!hit(x,y))return;
        pointer=Pointer::Surface;pointerTarget=snapshot;float left=static_cast<float>((CanvasWidth-motion.width.value)/2),py=y-BodyTop-static_cast<float>(motion.verticalOffset());
        if(motion.height.value>150) {
            if(std::abs(py-108)<18&&x>=left+24&&x<=left+motion.width.value-24){if(!snapshot->seek){cancel();return;}pointer=Pointer::Seek;}
            else if(std::abs(py-158)<26) {
                if(std::abs(x-CanvasWidth/2)<26){if(!snapshot->available||!(snapshot->pause||snapshot->play)){cancel();return;}pointer=Pointer::Play;}
                else if(std::abs(x-(CanvasWidth/2-70))<26){if(!snapshot->previous){cancel();return;}pointer=Pointer::Previous;}
                else if(std::abs(x-(CanvasWidth/2+70))<26){if(!snapshot->next){cancel();return;}pointer=Pointer::Next;}
            }
        }
        surfaceCloses=pointer==Pointer::Surface&&expansion.clicked();
        if(pointer==Pointer::Surface){pointerTarget.reset();POINT point=screenPoint(x,y);RECT rect{};GetWindowRect(window,&rect);if(!placement.initialized)placement.snap((rect.left+rect.right)/2.,rect.top,physicalScale);monitorInfo();monitorDrag.begin(point.x,point.y,placement.center.value,placement.top.value,physicalScale,py<88);hideGesture.begin(point.x/physicalScale,point.y/physicalScale,py<88);motion.surfacePress.target=1;}
        else {expansion.open();motion.setExpanded(true);}
        SetCapture(window);if(pointer==Pointer::Seek)drag(x,y);
    }
    void drag(float x,float y) {if(pointer==Pointer::Seek&&pointerTarget){auto bar=seekBar((CanvasWidth-motion.width.value)/2,motion.width.value,motion.seek.value);candidate=clamp01((x-bar.left)/bar.width)*pointerTarget->duration;}else if(pointer==Pointer::Surface){POINT point=screenPoint(x,y);if(monitorDrag.move(point.x,point.y,MonitorFromPoint(point,MONITOR_DEFAULTTONEAREST)!=homeMonitor)){hideGesture.cancel();placement.follow(monitorDrag.center,monitorDrag.top);}else hideGesture.move(point.x/monitorDrag.gestureScale(),point.y/monitorDrag.gestureScale());updatePresence(clockSeconds());}}
    void cancel() {bool returning=monitorDrag.active();monitorDrag.cancel();pointer=Pointer::None;pointerTarget.reset();hideGesture.cancel();if(returning)layoutPending=true;motion.tuck.target=manualHidden?1:0;motion.surfacePress.target=clockSeconds()<pressUntil?1:0;motion.seek.target=motion.playPress.target=motion.previousPress.target=motion.nextPress.target=0;if(GetCapture()==window)ReleaseCapture();}
    void send(Action action,double now,double value=0) {
        const Snapshot& target=pointerTarget?*pointerTarget:*snapshot;bool accepted=false;
        if(action==Action::Play||action==Action::Pause){bool playing=action==Action::Play;playback.request(playing,now,target.session,target.track);motion.playGlyph.target=playing?1:0;}
        if(preview) {
            auto s=std::make_shared<Snapshot>(*snapshot);if(action==Action::Play)s->playing=true;if(action==Action::Pause)s->playing=false;if(action==Action::Seek)s->position=value;
            if(action==Action::Next||action==Action::Previous){auto f=fixture(snapshot->track==L"one"?1:0,now);*s=f;}s->received=now;snapshot=s;accepted=true;
        }else if(media)accepted=media->command(action,target,value);
        if(action==Action::Next||action==Action::Previous){direction=action==Action::Next?1:-1;skipAt=now;(direction>0?motion.next:motion.previous).trigger(now);}
        if(action==Action::Seek&&accepted){pendingSeek=value;seekTrack=snapshot->track;seekUntil=now+2;}
    }
    void release(float x,float y,double now) {
        auto owner=pointer;if(owner==Pointer::None)return;if(owner==Pointer::Seek)drag(x,y);
        if(owner==Pointer::Surface){drag(x,y);POINT point=screenPoint(x,y);if(monitorDrag.active()){homeMonitor=MonitorFromPoint(point,MONITOR_DEFAULTTONEAREST);auto info=monitorInfo();settings.monitorDevice=info.szDevice;if(!testing)settings.save();cancel();layout();updatePresence(now);return;}auto gesture=hideGesture.finish(point.x/monitorDrag.gestureScale(),point.y/monitorDrag.gestureScale());if(gesture==HideGesture::Result::Hide){hide();return;}if(gesture==HideGesture::Result::Cancel){cancel();updatePresence(now);return;}}
        bool valid=owner==Pointer::Surface||(pointerTarget&&pointerTarget->track==snapshot->track&&pointerTarget->session==snapshot->session);
        float py=y-BodyTop-static_cast<float>(motion.verticalOffset()),center=CanvasWidth/2+(owner==Pointer::Previous?-70:owner==Pointer::Next?70:0);
        if(owner!=Pointer::Seek)valid=valid&&hit(x,y);
        if(owner!=Pointer::Seek&&owner!=Pointer::Surface)valid=valid&&std::abs(py-158)<29&&std::abs(x-center)<29;
        if(valid){if(owner==Pointer::Surface){pressUntil=now+.085;if(surfaceCloses&&!settings.pin){expandAfter=0;expansion.close();motion.setExpanded(false);}else {expansion.open();expandAfter=motion.expanded?0:pressUntil;}}else if(owner==Pointer::Seek)send(Action::Seek,now,candidate);else if(owner==Pointer::Play)send(effectivePlaying(now)?Action::Pause:Action::Play,now);else send(owner==Pointer::Next?Action::Next:Action::Previous,now);}
        cancel();
    }
    bool startupEnabled() const {HKEY key=nullptr;if(RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,KEY_QUERY_VALUE,&key)!=ERROR_SUCCESS)return false;DWORD size=0;bool exists=RegQueryValueExW(key,L"MusicIslandWindows",nullptr,nullptr,nullptr,&size)==ERROR_SUCCESS;RegCloseKey(key);return exists;}
    void toggleStartup() {bool enabled=startupEnabled();HKEY key=nullptr;if(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS)return;if(enabled)RegDeleteValueW(key,L"MusicIslandWindows");else {auto value=L"\""+exe+L"\"";RegSetValueExW(key,L"MusicIslandWindows",0,REG_SZ,reinterpret_cast<const BYTE*>(value.c_str()),static_cast<DWORD>((value.size()+1)*sizeof(wchar_t)));}RegCloseKey(key);}
    void menu(POINT point) {
        menuActive=true;
        HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING|(settings.pin?MF_CHECKED:0),1,L"Keep expanded");AppendMenuW(menu,MF_STRING|(settings.reduced?MF_CHECKED:0),2,L"Reduce motion");AppendMenuW(menu,MF_STRING|(settings.hover?MF_CHECKED:0),8,L"Expand on hover");AppendMenuW(menu,MF_STRING|(settings.arrows?MF_CHECKED:0),10,L"Arrow-key controls (typing protected)");AppendMenuW(menu,MF_STRING,3,L"Smaller");AppendMenuW(menu,MF_STRING,4,L"Larger");AppendMenuW(menu,MF_STRING|(startupEnabled()?MF_CHECKED:0),6,L"Start with Windows");AppendMenuW(menu,MF_STRING,9,manualHidden?L"Show island":L"Hide island");AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,7,L"Exit MusicIsland");SetForegroundWindow(window);auto selected=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,window,nullptr);DestroyMenu(menu);PostMessageW(window,WM_NULL,0,0);
        menuActive=false;
        if(selected==1){settings.pin=!settings.pin;if(settings.pin)expansion.open();else expansion.close();motion.setExpanded(settings.pin);}
        if(selected==2)settings.reduced=!settings.reduced;
        if(selected==8)settings.hover=!settings.hover;
        if(selected==10){settings.arrows=!settings.arrows;typing.setEnabled(settings.arrows);}
        if(selected==9){if(manualHidden)reveal();else if(snapshot->available)hide();}
        if(selected==3||selected==4){settings.scale=std::clamp(settings.scale+(selected==3?-.1:.1),.75,1.4);layoutPending=true;}
        if(selected==6)toggleStartup();if(selected==7)DestroyWindow(window);settings.save();
    }
};
static LRESULT CALLBACK procedure(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
    auto app=reinterpret_cast<App*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(message==WM_NCCREATE){app=static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);app->window=hwnd;SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));}
    if(!app)return DefWindowProcW(hwnd,message,wp,lp);
    if(message==app->taskbarCreated&&app->taskbarCreated){app->addTray();return 0;}
    switch(message) {
    case WM_PAINT:{PAINTSTRUCT ps;BeginPaint(hwnd,&ps);EndPaint(hwnd,&ps);return 0;}
    case WM_ERASEBKGND:return 1;
    case WM_MOUSEACTIVATE:return MA_NOACTIVATE;
    case WM_NCHITTEST:{POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(hwnd,&p);return app->hit(p.x/app->physicalScale,p.y/app->physicalScale)?HTCLIENT:HTTRANSPARENT;}
    case WM_LBUTTONDOWN:app->press(GET_X_LPARAM(lp)/app->physicalScale,GET_Y_LPARAM(lp)/app->physicalScale,clockSeconds());return 0;
    case WM_MOUSEMOVE:app->drag(GET_X_LPARAM(lp)/app->physicalScale,GET_Y_LPARAM(lp)/app->physicalScale);return 0;
    case WM_LBUTTONUP:app->release(GET_X_LPARAM(lp)/app->physicalScale,GET_Y_LPARAM(lp)/app->physicalScale,clockSeconds());return 0;
    case WM_CAPTURECHANGED:app->cancel();return 0;
    case WM_RBUTTONUP:case WM_CONTEXTMENU:{POINT p;GetCursorPos(&p);app->menu(p);return 0;}
    case MediaMessage:app->updateSnapshot();return 0;
    case OutsideMessage:app->closeFromOutside();return 0;
    case RevealMessage:app->reveal();return 0;
    case ArrowMessage:if(reinterpret_cast<HWND>(lp)==GetForegroundWindow()&&app->typing.allowsArrows())app->arrow(static_cast<unsigned>(wp),clockSeconds());return 0;
    case WM_DISPLAYCHANGE:app->cancel();app->homeMonitor=nullptr;app->layoutPending=true;return 0;
    case WM_DPICHANGED:case WM_SETTINGCHANGE:app->layoutPending=true;return 0;
    case WM_HOTKEY:app->toggleExpanded();return 0;
    case TrayMessage:if(LOWORD(lp)==WM_CONTEXTMENU||LOWORD(lp)==WM_RBUTTONUP){POINT p;GetCursorPos(&p);app->menu(p);}else if(LOWORD(lp)==NIN_SELECT||LOWORD(lp)==WM_LBUTTONUP)app->toggleExpanded();return 0;
    case WM_CLOSE:DestroyWindow(hwnd);return 0;
    case WM_DESTROY:Shell_NotifyIconW(NIM_DELETE,&app->tray);UnregisterHotKey(hwnd,1);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(hwnd,message,wp,lp);
}
static LRESULT CALLBACK outsideClick(int code,WPARAM message,LPARAM data) {
    if(code==HC_ACTION&&hookedApp&&hookedApp->edgeMouse(static_cast<UINT>(message),reinterpret_cast<MSLLHOOKSTRUCT*>(data)->pt))return 1;
    if(code==HC_ACTION&&hookedApp&&(message==WM_LBUTTONDOWN||message==WM_RBUTTONDOWN||message==WM_MBUTTONDOWN)) {
        auto& app=*hookedApp;
        if(app.settings.arrows)app.typing.invalidate();
        if(app.motion.expanded&&!app.menuActive&&app.pointer==Pointer::None&&!app.settings.pin) {
            POINT screen=reinterpret_cast<MSLLHOOKSTRUCT*>(data)->pt,local=screen;ScreenToClient(app.window,&local);
            if(!app.hit(local.x/app.physicalScale,local.y/app.physicalScale)) {
                NOTIFYICONIDENTIFIER icon{sizeof(icon)};icon.hWnd=app.window;icon.uID=1;RECT tray{};
                if(FAILED(Shell_NotifyIconGetRect(&icon,&tray))||!PtInRect(&tray,screen))PostMessageW(app.window,OutsideMessage,0,0);
            }
        }
    }
    return CallNextHookEx(nullptr,code,message,data);
}
static void CALLBACK keyboardFocus(HWINEVENTHOOK,DWORD,HWND,LONG,LONG,DWORD,DWORD){if(hookedApp&&hookedApp->settings.arrows)hookedApp->typing.invalidate();}
static LRESULT CALLBACK arrowKeyHook(int code,WPARAM message,LPARAM data) {
    if(code==HC_ACTION&&hookedApp){
        auto& app=*hookedApp;auto key=reinterpret_cast<KBDLLHOOKSTRUCT*>(data);
        bool down=message==WM_KEYDOWN||message==WM_SYSKEYDOWN,up=message==WM_KEYUP||message==WM_SYSKEYUP;
        bool injected=(key->flags&LLKHF_INJECTED)!=0;
        if(down||up){
            if(!injected)app.typing.noteKey(key->vkCode,down);
            if(arrowAction(key->vkCode)!=ArrowAction::None){
                bool modified=(key->flags&LLKHF_ALTDOWN)||(GetAsyncKeyState(VK_CONTROL)&0x8000)||(GetAsyncKeyState(VK_SHIFT)&0x8000)||(GetAsyncKeyState(VK_LWIN)&0x8000)||(GetAsyncKeyState(VK_RWIN)&0x8000);
                bool allowed=down&&!modified&&app.arrowAvailable(key->vkCode)&&app.typing.allowsArrows();
                auto result=app.arrowKeys.event(key->vkCode,down,allowed,injected);
                if(result.trigger&&!PostMessageW(app.window,ArrowMessage,key->vkCode,reinterpret_cast<LPARAM>(GetForegroundWindow())))return CallNextHookEx(nullptr,code,message,data);
                if(result.consume)return 1;
            }
        }
    }
    return CallNextHookEx(nullptr,code,message,data);
}
static int renderTest(App& app,const std::filesystem::path& output) {
    HWND window=app.window;app.preview=app.testing=true;app.settings.pin=false;app.settings.hover=false;app.motion.presence.snap(true);app.snapshot=std::make_shared<Snapshot>(fixture(0,clockSeconds()));
    app.settings.arrows=false;app.arrow(VK_RIGHT,clockSeconds());
    if(app.snapshot->track!=L"one")throw std::runtime_error("Disabled arrow controls changed playback");
    app.settings.arrows=true;app.arrow(VK_RIGHT,clockSeconds());
    if(app.snapshot->track!=L"two"||app.direction!=1)throw std::runtime_error("Right arrow did not advance and animate the next song");
    app.arrow(VK_LEFT,clockSeconds());if(app.snapshot->track!=L"one"||app.direction!=-1)throw std::runtime_error("Left arrow did not select and animate the preceding song");
    app.arrow(VK_UP,clockSeconds());if(app.effectivePlaying(clockSeconds()))throw std::runtime_error("Up arrow did not immediately pause");
    app.arrow(VK_UP,clockSeconds());if(!app.effectivePlaying(clockSeconds()))throw std::runtime_error("A rapid second up arrow did not immediately resume");
    app.arrow(VK_DOWN,clockSeconds());if(!app.expansion.clicked())throw std::runtime_error("Down arrow did not open the island");
    app.arrow(VK_DOWN,clockSeconds());if(app.expansion.clicked())throw std::runtime_error("Down arrow did not close the island");
    app.hide();app.arrow(VK_DOWN,clockSeconds());if(app.manualHidden)throw std::runtime_error("Down arrow did not restore the hidden island");
    auto limited=std::make_shared<Snapshot>(*app.snapshot);limited->next=false;app.snapshot=limited;app.arrow(VK_RIGHT,clockSeconds());
    if(app.snapshot->track!=L"one"||app.arrowAvailable(VK_RIGHT))throw std::runtime_error("Arrow controls ignored player capabilities");
    app.snapshot=std::make_shared<Snapshot>();if(app.arrowAvailable(VK_DOWN))throw std::runtime_error("No-music state intercepted arrow controls");
    HWND edit=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|ES_MULTILINE,0,0,100,24,window,nullptr,GetModuleHandleW(nullptr),nullptr);
    HWND password=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|ES_PASSWORD,0,0,100,24,window,nullptr,GetModuleHandleW(nullptr),nullptr);
    HWND button=CreateWindowExW(0,L"BUTTON",L"Test",WS_CHILD,0,0,100,24,window,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!edit||!password||!button||!nativeTextFocus(edit,nullptr)||!nativeTextFocus(password,nullptr)||nativeTextFocus(button,nullptr)||!nativeTextFocus(button,edit)||automationAllowsArrows(nullptr))throw std::runtime_error("Native text, password, caret or unknown focus was not protected");
    DestroyWindow(edit);DestroyWindow(password);DestroyWindow(button);
    app.typing.setEnabled(false);if(app.typing.allowsArrows())throw std::runtime_error("Disabled typing guard approved an arrow");
    app.typing.setEnabled(true);app.typing.noteKey('A',true);
    if(app.typing.allowsArrows())throw std::runtime_error("Recent typing allowed music arrows");
    app.typing.invalidate();if(app.typing.allowsArrows())throw std::runtime_error("Changing focus discarded the recent-typing protection");
    app.typing.setEnabled(false);
    app.settings.arrows=false;app.snapshot=std::make_shared<Snapshot>(fixture(0,clockSeconds()));app.playback=PlaybackFeedback{};app.direction=0;app.skipAt=-10;app.motion=Motion{};app.motion.presence.snap(true);app.manualHidden=false;app.shown=false;app.expansion.close();
    SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(220,36));app.updateExpansion(clockSeconds(),true);
    if(app.motion.expanded||app.expansion.clicked()||app.motion.surfacePress.target!=1)throw std::runtime_error("Mouse-down expanded the island instead of showing only press feedback");
    SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(220,36));double released=clockSeconds();app.updateExpansion(released+.04,true);
    if(!app.expansion.clicked()||app.motion.expanded||app.motion.surfacePress.target!=1)throw std::runtime_error("Release skipped the click animation before expansion");
    app.updateExpansion(released+.1,true);if(!app.motion.expanded)throw std::runtime_error("Release did not expand after the click animation");
    app.expansion=ExpansionController{};
    app.expansion.update(10,true,false,false,true);app.expansion.update(10.2,true,false,false,true);app.motion.setExpanded(true);for(int i=0;i<180;i++)app.motion.step(1.0/120);
    auto click=[&](int x,int y){SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(x,y));};
    click(95,70);
    if(!app.expansion.clicked()||!app.motion.expanded||!app.expansion.update(30,false,false,false))throw std::runtime_error("Hover followed by a native click collapsed the island");
    click(95,70);if(app.motion.expanded||app.expansion.update(31,true,false,false))throw std::runtime_error("Explicit native close reopened immediately");
    // Every tap reverses the target, including clicks before any frame is drawn.
    for(int i=0;i<180;i++)app.motion.step(1.0/120);
    for(int i=0;i<40;i++){
        double height=app.motion.height.value,velocity=app.motion.height.velocity;click(220,36);
        if(app.expansion.clicked()!=(i%2==0))throw std::runtime_error("A rapid background click was ignored");
        if(app.motion.height.value!=height||app.motion.height.velocity!=velocity)throw std::runtime_error("A rapid click snapped the spring instead of reversing it");
        if(i%4)app.motion.step(1.0/240);
    }
    SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(220,36));app.snapshot=std::make_shared<Snapshot>(fixture(1,clockSeconds()));app.updateSnapshot();SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(220,36));
    if(!app.expansion.clicked()||app.pointer!=Pointer::None)throw std::runtime_error("A track update canceled a background click");
    app.updateExpansion(clockSeconds()+.1,false);for(int i=0;i<180;i++)app.motion.step(1.0/120);
    SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(220,126));SendMessageW(window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(360,200));SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(360,200));
    if(app.pointer!=Pointer::None||app.snapshot->position<200||!app.motion.expanded)throw std::runtime_error("Captured seeking failed when releasing away from the slider");
    hookedApp=&app;MSLLHOOKSTRUCT outside{};outside.pt={700,700};outsideClick(HC_ACTION,WM_LBUTTONDOWN,reinterpret_cast<LPARAM>(&outside));MSG outsideMessage{};
    if(!PeekMessageW(&outsideMessage,window,OutsideMessage,OutsideMessage,PM_REMOVE))throw std::runtime_error("Outside click was not detected");DispatchMessageW(&outsideMessage);hookedApp=nullptr;
    if(app.motion.expanded)throw std::runtime_error("Outside click did not collapse the island");
    // Exercise a real control gesture while its synthetic player keeps reporting
    // the old status and rejects the command. No real media session is modified.
    app.preview=false;auto slow=std::make_shared<Snapshot>(fixture(0,clockSeconds()));slow->play=false;app.snapshot=slow;
    app.expansion.open();app.motion.setExpanded(true);app.motion.playGlyph.snap(1);for(int i=0;i<180;i++)app.motion.step(1./120);
    click(220,176);double pauseTime=clockSeconds();
    if(app.motion.playGlyph.target!=0||app.effectivePlaying(pauseTime))throw std::runtime_error("Pause gesture waited for the player instead of animating immediately");
    slow->error=L"The player did not accept that control";
    for(int i=1;i<=120;i++){app.updateSnapshot();app.motion.playGlyph.target=app.effectivePlaying(pauseTime+i/120.)?1:0;app.motion.step(1./120);}
    if(app.motion.playGlyph.value!=0||app.motion.playGlyph.moving())throw std::runtime_error("A stale or rejected media response interrupted the pause animation");
    double glyphPosition=app.motion.playGlyph.value,glyphVelocity=app.motion.playGlyph.velocity;click(220,176);
    if(app.motion.playGlyph.target!=1||app.motion.playGlyph.value!=glyphPosition||app.motion.playGlyph.velocity!=glyphVelocity)throw std::runtime_error("Stale play capability ignored a repeat control gesture");
    click(220,176);if(app.motion.playGlyph.target!=0)throw std::runtime_error("Rapid pause/play reversal ignored a control gesture");
    if(!app.effectivePlaying(pauseTime+4))throw std::runtime_error("Failed playback feedback did not reconcile to the real player");
    app.preview=true;app.snapshot=std::make_shared<Snapshot>(fixture(0,clockSeconds()));app.expansion.close();app.expandAfter=app.pressUntil=0;app.motion.surfacePress.snap(0);app.motion.setExpanded(false);for(int i=0;i<180;i++)app.motion.step(1./120);
    RECT anchored{};GetWindowRect(window,&anchored);
    SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(220,36));SendMessageW(window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(280,36));SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(280,36));
    if(app.manualHidden||app.expansion.clicked())throw std::runtime_error("A horizontal drag moved, hid or expanded the island");
    SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(220,36));SendMessageW(window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(220,26));SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(220,26));
    if(app.manualHidden||app.expansion.clicked()||app.motion.presence.progress.target!=1)throw std::runtime_error("A canceled upward pull did not spring back without expanding");
    SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(220,36));SendMessageW(window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(220,16));
    if(app.manualHidden||app.motion.presence.progress.target>=1||app.motion.tuck.target<=0)throw std::runtime_error("An upward pull did not preview hiding before release");
    SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(220,16));app.updateSnapshot();
    if(!app.manualHidden||app.motion.presence.progress.target!=0||app.motion.peek.target!=1||app.pointer!=Pointer::None)throw std::runtime_error("An upward swipe did not leave only the subtle restore handle");
    RECT stillAnchored{};GetWindowRect(window,&stillAnchored);if(!EqualRect(&anchored,&stillAnchored))throw std::runtime_error("A gesture changed the island's native window position");
    hookedApp=&app;MONITORINFO edgeMonitor{sizeof(edgeMonitor)};GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTOPRIMARY),&edgeMonitor);
    MSLLHOOKSTRUCT edge{};edge.pt={(anchored.left+anchored.right)/2,edgeMonitor.rcMonitor.top};
    POINT elsewhere{edge.pt.x+200,edge.pt.y};if(app.edgeMouse(WM_LBUTTONDOWN,elsewhere))throw std::runtime_error("The restore area intercepted an unrelated click");
    if(outsideClick(HC_ACTION,WM_LBUTTONDOWN,reinterpret_cast<LPARAM>(&edge))!=1||outsideClick(HC_ACTION,WM_LBUTTONUP,reinterpret_cast<LPARAM>(&edge))!=1)throw std::runtime_error("An edge click leaked through to the underlying application");
    MSG revealMessage{};if(!PeekMessageW(&revealMessage,window,RevealMessage,RevealMessage,PM_REMOVE))throw std::runtime_error("Clicking the top edge did not request restoration");DispatchMessageW(&revealMessage);
    if(app.manualHidden||app.motion.presence.progress.target!=1||app.motion.tuck.target!=0||app.expansion.clicked())throw std::runtime_error("Edge click did not restore the compact island");
    app.hide();if(outsideClick(HC_ACTION,WM_LBUTTONDOWN,reinterpret_cast<LPARAM>(&edge))!=1)throw std::runtime_error("Downward edge gesture did not start");
    edge.pt.y+=20;outsideClick(HC_ACTION,WM_MOUSEMOVE,reinterpret_cast<LPARAM>(&edge));
    if(!PeekMessageW(&revealMessage,window,RevealMessage,RevealMessage,PM_REMOVE))throw std::runtime_error("Pulling down from the edge did not restore the island");DispatchMessageW(&revealMessage);
    if(outsideClick(HC_ACTION,WM_LBUTTONUP,reinterpret_cast<LPARAM>(&edge))!=1||app.manualHidden)throw std::runtime_error("A downward edge swipe did not finish cleanly");
    app.hide();app.snapshot=std::make_shared<Snapshot>();edge.pt.y=edgeMonitor.rcMonitor.top;
    if(app.edgeMouse(WM_LBUTTONDOWN,edge.pt))throw std::runtime_error("No-music state intercepted a top-edge click");
    hookedApp=nullptr;app.manualHidden=false;app.snapshot=std::make_shared<Snapshot>(fixture(0,clockSeconds()));app.motion.presence.snap(true);app.motion.tuck.snap(0);app.motion.peek.snap(0);app.motion.surfacePress.snap(0);app.updateSnapshot();
    std::filesystem::create_directories(output);app.renderer=std::make_unique<Renderer>(window,144.f);Renderer& renderer=*app.renderer;Motion motion;motion.presence.snap(true);motion.playGlyph.snap(1);auto a=fixture(0,0),b=fixture(1,0);Snapshot* current=&a;double total=0;
    for(int i=0;i<240;i++) {
        double now=i/120.0;if(i==20)motion.setExpanded(true);if(i==90){motion.previous.trigger(now);motion.next.trigger(now);motion.playGlyph.target=0;}if(i==110){current=&b;b.received=now;}if(i==130)motion.setExpanded(false);if(i==142)motion.setExpanded(true);if(i==155)motion.seek.target=1;if(i==190)motion.seek.target=0;if(i==215)motion.playGlyph.target=1;
        motion.step(1.0/120);current->received=current->audioReceived=now;current->audioSource=current->source;for(size_t band=0;band<9;band++)current->peaks[band]=static_cast<float>(.1+.75*std::pow(.5+.5*std::sin(now*(3+band*.37)+band*1.3),2));double start=clockSeconds();renderer.render(motion,*current,now,62,motion.playGlyph.target==1,true,1,false);total+=clockSeconds()-start;
        if(i==10||i==75||i==103||i==120||i==138||i==175||i==230)renderer.save((output/(L"frame-"+std::to_wstring(i)+L".png")).wstring());
    }
    renderer.resize(288);for(int i=0;i<120;i++)motion.step(1.0/120);b.audioReceived=3;renderer.render(motion,b,3,62,true,true,0,false);renderer.save((output/L"high-dpi.png").wstring());
    auto unavailable=b;unavailable.play=unavailable.pause=false;motion.playGlyph.snap(0);
    renderer.render(motion,unavailable,3.1,62,false,false,0,false);renderer.save((output/L"pause-stale-capabilities.png").wstring());
    motion.playGlyph.snap(1);renderer.render(motion,unavailable,3.2,62,true,false,0,false);renderer.save((output/L"play-stale-capabilities.png").wstring());
    motion.setExpanded(false);motion.presence.request(false);motion.playGlyph.target=0;Snapshot empty;
    for(int i=0;i<120;i++){motion.step(1.0/120);renderer.render(motion,empty,4+i/120.0,0,false,false,0,false);}renderer.save((output/L"idle-visible.png").wstring());
    motion.presence.snap(true);motion.setExpanded(true);motion.playGlyph.snap(1);for(int i=0;i<180;i++)motion.step(1.0/120);
    renderer.render(motion,a,10,62,true,true,0,false);renderer.render(motion,a,10.5,62,true,true,0,false);
    renderer.save((output/L"cover-before.png").wstring());
    renderer.render(motion,b,11,62,true,true,1,false);renderer.save((output/L"cover-start.png").wstring());
    renderer.render(motion,b,11.21,62,true,true,1,false);renderer.save((output/L"cover-midpoint.png").wstring());
    renderer.render(motion,a,11.21,62,true,true,-1,false);renderer.save((output/L"cover-retarget.png").wstring());
    renderer.render(motion,a,12,62,true,true,0,false);renderer.render(motion,b,13,62,true,true,1,false);renderer.render(motion,b,13.5,62,true,true,1,false);renderer.save((output/L"cover-after.png").wstring());
    a.track=L"long";a.title=L"A very long song title that scrolls smoothly through compact and expanded layouts";a.audioSource=a.source;
    motion.setExpanded(false);for(int i=0;i<600;i++){double now=15+i/120.0;motion.step(1.0/120);renderer.render(motion,a,now,62,true,true,0,false);}
    motion.setExpanded(true);for(int i=0;i<100;i++){double now=20+i/120.0;motion.step(1.0/120);renderer.render(motion,a,now,62,true,true,0,false);if(i==0||i==15||i==60)renderer.save((output/(L"title-expand-"+std::to_wstring(i)+L".png")).wstring());}
    motion.setExpanded(false);for(int i=0;i<180;i++)motion.step(1./120);motion.presence.snap(false);motion.presence.request(true);
    for(int i=0;i<120;i++){
        double now=25+i/120.;motion.step(1./120);renderer.render(motion,b,now,62,true,true,0,false);
        if(i==2||i==12||i==36||i==119)renderer.save((output/(L"entrance-"+std::to_wstring(i)+L".png")).wstring());
    }
    motion.presence.request(false);for(int i=0;i<20;i++){motion.step(1./120);renderer.render(motion,b,26+i/120.,62,true,true,0,false);}
    double appearance=motion.presence.progress.value,velocity=motion.presence.progress.velocity;motion.presence.request(true);
    if(motion.presence.progress.value!=appearance||motion.presence.progress.velocity!=velocity)throw std::runtime_error("A song arriving during dismissal jumped the entrance animation");
    for(int i=0;i<120;i++){motion.step(1./120);renderer.render(motion,b,26.2+i/120.,62,true,true,0,false);}
    motion.setExpanded(true);motion.seek.snap(0);for(int i=0;i<180;i++)motion.step(1./120);
    renderer.render(motion,b,30,62,true,true,0,false);renderer.save((output/L"slider-rest.png").wstring());
    motion.seek.target=1;
    for(int i=0;i<60;i++){motion.step(1./120);renderer.render(motion,b,30+(i+1)/120.,62+100*smooth(i/59.),true,true,0,false);if(i==12||i==59)renderer.save((output/(L"slider-drag-"+std::to_wstring(i)+L".png")).wstring());}
    motion.seek.target=0;
    for(int i=0;i<60;i++){motion.step(1./120);renderer.render(motion,b,30.5+(i+1)/120.,162,true,true,0,false);if(i==5||i==59)renderer.save((output/(L"slider-release-"+std::to_wstring(i)+L".png")).wstring());}
    Motion manual;manual.presence.snap(true);manual.playGlyph.snap(1);renderer.render(manual,b,35,62,true,true,0,false);
    manual.surfacePress.snap(1);renderer.render(manual,b,35.001,62,true,true,0,false);renderer.save((output/L"island-pressed.png").wstring());manual.surfacePress.snap(0);
    manual.tuck.target=manual.peek.target=1;manual.presence.request(false);
    for(int i=0;i<120;i++){manual.step(1./120);renderer.render(manual,b,35+(i+1)/120.,62,true,true,0,false);if(i==15||i==40||i==119)renderer.save((output/(L"manual-hide-"+std::to_wstring(i)+L".png")).wstring());}
    manual.tuck.target=manual.peek.target=0;manual.presence.request(true);
    for(int i=0;i<120;i++){manual.step(1./120);renderer.render(manual,b,36+(i+1)/120.,62,true,true,0,false);if(i==15||i==40||i==119)renderer.save((output/(L"edge-restore-"+std::to_wstring(i)+L".png")).wstring());}
    // Drive the production show/hide state with synthetic song arrival/removal.
    renderer.resize(96);app.physicalScale=1;app.layoutPending=false;app.shown=false;app.motion.presence.snap(false);app.expansion.close();app.motion.setExpanded(false);app.preview=true;
    app.snapshot=std::make_shared<Snapshot>();app.updateSnapshot();
    if(app.shown||app.hit(220,36))throw std::runtime_error("An empty media session appeared or intercepted clicks");
    app.snapshot=std::make_shared<Snapshot>(fixture(0,clockSeconds()));app.updateSnapshot();
    if(!app.shown||app.motion.presence.progress.target!=1||app.motion.presence.progress.value!=0)throw std::runtime_error("A song did not start the entrance from a hidden dot");
    double visibilityTime=clockSeconds();for(int i=0;i<120;i++)app.tick(visibilityTime+i/120.,false);
    if(app.motion.expanded)throw std::runtime_error("Default hover opened the island after its entrance");
    app.snapshot=std::make_shared<Snapshot>();app.updateSnapshot();
    if(!app.shown||app.displaySnapshot->title.empty()||app.hit(220,36))throw std::runtime_error("Dismissal discarded its cover/title or kept intercepting clicks");
    for(int i=0;i<180;i++)app.tick(visibilityTime+1+i/120.,false);
    if(app.shown||!app.motion.presence.hidden())throw std::runtime_error("No-music dismissal did not hide the native island");
    // Transfer a hidden production window between actual connected displays.
    std::vector<HMONITOR> displays;
    EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR monitor,HDC,LPRECT,LPARAM data)->BOOL{reinterpret_cast<std::vector<HMONITOR>*>(data)->push_back(monitor);return TRUE;},reinterpret_cast<LPARAM>(&displays));
    bool nativeMonitorTransfer=false;
    if(displays.size()>1){
        app.homeMonitor=displays[0];app.shown=false;app.manualHidden=false;app.snapshot=std::make_shared<Snapshot>(fixture(0,clockSeconds()));app.motion=Motion{};app.motion.presence.snap(true);app.layout();app.shown=true;
        MONITORINFO destination{sizeof(destination)};GetMonitorInfoW(displays[1],&destination);
        app.press(220,36,clockSeconds());
        POINT drop{(destination.rcWork.left+destination.rcWork.right)/2,destination.rcWork.top+100};POINT local=drop;ScreenToClient(window,&local);app.drag(local.x/app.physicalScale,local.y/app.physicalScale);
        if(!app.monitorDrag.active())throw std::runtime_error("A header drag into another display did not start transfer");
        double transferTime=clockSeconds();for(int i=0;i<120;i++)app.tick(transferTime+i/120.,false);
        local=drop;ScreenToClient(window,&local);double beforeDrop=app.placement.center.value;app.release(local.x/app.physicalScale,local.y/app.physicalScale,transferTime+1);
        if(app.homeMonitor!=displays[1]||app.manualHidden||app.pointer!=Pointer::None||app.placement.center.value!=beforeDrop)throw std::runtime_error("Monitor drop chose the wrong display, hid the island or jumped its position");
        for(int i=0;i<360;i++)app.tick(transferTime+1+i/120.,false);
        RECT docked{};GetWindowRect(window,&docked);
        if(std::abs((docked.left+docked.right)/2.-(destination.rcWork.left+destination.rcWork.right)/2.)>1||std::abs(docked.top-destination.rcWork.top-2)>1||app.placement.moving())throw std::runtime_error("Monitor transfer did not settle at the selected display's top center");
        nativeMonitorTransfer=true;
    }
    app.renderer.reset();
    std::ofstream report(output/L"render.json");report<<"{\"frames\":"<<(1865+(nativeMonitorTransfer?480:0))<<",\"nativeClickChecks\":53,\"arrowControlChecks\":true,\"nativeTypingChecks\":true,\"elasticMonitorMotionChecks\":true,\"connectedMonitors\":"<<displays.size()<<",\"nativeMonitorTransferChecks\":"<<(nativeMonitorTransfer?"true":"false")<<",\"edgeSwipeChecks\":true,\"releaseClickChecks\":true,\"presenceChecks\":true,\"delayedPlaybackChecks\":true,\"meanRenderMs\":"<<total/240*1000<<",\"highDpi\":288,\"renderer\":\"Direct2D/DirectComposition\",\"passed\":true}\n";return 0;
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int) {
    using namespace island;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);winrt::init_apartment(winrt::apartment_type::single_threaded);
    int count=0;LPWSTR* args=CommandLineToArgvW(GetCommandLineW(),&count);std::wstring testOutput,probeOutput;bool preview=false;
    for(int i=1;i<count;i++){if(std::wstring(args[i])==L"--preview")preview=true;if(std::wstring(args[i])==L"--render-test"&&i+1<count)testOutput=args[++i];if(std::wstring(args[i])==L"--probe-media"&&i+1<count)probeOutput=args[++i];}LocalFree(args);
    HANDLE mutex=nullptr;
    if(testOutput.empty()&&probeOutput.empty()){mutex=CreateMutexW(nullptr,FALSE,L"Local\\MusicIslandWindows.Native.v1");if(GetLastError()==ERROR_ALREADY_EXISTS){if(auto existing=FindWindowW(L"MusicIsland.Native.Window",nullptr))PostMessageW(existing,WM_HOTKEY,1,0);CloseHandle(mutex);return 0;}}
    try {
        App app;app.preview=preview;wchar_t path[32768];GetModuleFileNameW(nullptr,path,32768);app.exe=path;
        WNDCLASSEXW cls{sizeof(cls)};cls.style=CS_HREDRAW|CS_VREDRAW;cls.hInstance=instance;cls.lpfnWndProc=procedure;cls.lpszClassName=L"MusicIsland.Native.Window";cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassExW(&cls);
        app.window=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOREDIRECTIONBITMAP,cls.lpszClassName,L"MusicIsland",WS_POPUP,0,0,440,264,nullptr,nullptr,instance,&app);if(!app.window)throw std::runtime_error("Could not create the native window");
        app.typing.setWindow(app.window);
        if(!testOutput.empty()){int result=renderTest(app,testOutput);DestroyWindow(app.window);return result;}
        if(!probeOutput.empty()) {
            auto parent=std::filesystem::path(probeOutput).parent_path();if(!parent.empty())std::filesystem::create_directories(parent);MediaService media(app.window);std::shared_ptr<const Snapshot> snapshot;
            for(int i=0;i<120;i++){Sleep(50);snapshot=media.snapshot();if(snapshot->available)break;}
            AudioPeaks maxima{};double peak=0;for(int i=0;i<80;i++){Sleep(50);snapshot=media.snapshot();peak=std::max(peak,snapshot->peak);for(size_t b=0;b<9;b++)maxima[b]=std::max(maxima[b],snapshot->peaks[b]);}
            std::ofstream report(probeOutput);report<<"available="<<snapshot->available<<"\nplaying="<<snapshot->playing<<"\nsource="<<winrt::to_string(snapshot->source)<<"\nduration="<<snapshot->duration<<"\nartworkBytes="<<(snapshot->artwork?snapshot->artwork->size():0)<<"\nprevious="<<snapshot->previous<<"\nnext="<<snapshot->next<<"\nseek="<<snapshot->seek<<"\nerror="<<winrt::to_string(snapshot->error)<<"\naudioAvailable="<<snapshot->audioAvailable<<"\naudioPid="<<snapshot->audioPid<<"\naudioMatches="<<snapshot->audioMatches<<"\naudioSamples="<<snapshot->audioSamples<<"\npeak="<<peak<<"\npeakMaxima=";for(double value:maxima)report<<value<<',';report<<'\n';DestroyWindow(app.window);return snapshot->error.empty()?0:2;
        }
        app.physicalScale=GetDpiForWindow(app.window)/96.f;app.renderer=std::make_unique<Renderer>(app.window,96*app.physicalScale);app.taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");app.addTray();RegisterHotKey(app.window,1,MOD_WIN|MOD_ALT|MOD_NOREPEAT,'M');
        if(preview)app.snapshot=std::make_shared<Snapshot>(fixture(0,clockSeconds()));else app.media=std::make_unique<MediaService>(app.window);
        app.motion.playGlyph.snap(app.snapshot->playing?1:0);app.motion.setExpanded(app.settings.pin);app.layout();app.updatePresence(clockSeconds());
        hookedApp=&app;app.mouseHook=SetWindowsHookExW(WH_MOUSE_LL,outsideClick,instance,0);if(!app.mouseHook)throw std::runtime_error("Could not monitor outside clicks");
        app.keyboardHook=SetWindowsHookExW(WH_KEYBOARD_LL,arrowKeyHook,instance,0);
        app.focusHook=SetWinEventHook(EVENT_OBJECT_FOCUS,EVENT_OBJECT_FOCUS,nullptr,keyboardFocus,0,0,WINEVENT_OUTOFCONTEXT);
        app.foregroundHook=SetWinEventHook(EVENT_SYSTEM_FOREGROUND,EVENT_SYSTEM_FOREGROUND,nullptr,keyboardFocus,0,0,WINEVENT_OUTOFCONTEXT);
        if(!app.keyboardHook||!app.focusHook||!app.foregroundHook)throw std::runtime_error("Could not install typing-safe arrow controls");
        app.typing.setEnabled(app.settings.arrows);
        bool running=true;double lastTopmost=0;
        while(running) {
            HANDLE frame=app.renderer->frameWait();bool animated=app.needsFrames();DWORD handles=animated?1:0;
            DWORD result=MsgWaitForMultipleObjectsEx(handles,&frame,INFINITE,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
            if(result==WAIT_FAILED)throw std::runtime_error("Could not wait for the display frame");
            MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(message.message==WM_QUIT){running=false;break;}TranslateMessage(&message);DispatchMessageW(&message);}
            if(!running)break;
            double now=clockSeconds();if(app.shown&&((animated&&result==WAIT_OBJECT_0)||(!animated&&app.needsFrames())))app.tick(now);
            if(app.shown&&now-lastTopmost>5){SetWindowPos(app.window,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);lastTopmost=now;}
        }
        app.media.reset();app.renderer.reset();
    }catch(std::exception const& error) {
        if(!testOutput.empty()){std::filesystem::create_directories(testOutput);std::ofstream report(std::filesystem::path(testOutput)/L"failure.txt");report<<error.what();}
        else MessageBoxA(nullptr,error.what(),"MusicIsland could not start",MB_OK|MB_ICONERROR);
        if(mutex)CloseHandle(mutex);return 1;
    }catch(winrt::hresult_error const& error) {
        if(!testOutput.empty()){std::filesystem::create_directories(testOutput);std::ofstream report(std::filesystem::path(testOutput)/L"failure.txt");report<<winrt::to_string(error.message());}
        else MessageBoxW(nullptr,error.message().c_str(),L"MusicIsland could not start",MB_OK|MB_ICONERROR);if(mutex)CloseHandle(mutex);return 1;
    }
    if(mutex)CloseHandle(mutex);winrt::uninit_apartment();return 0;
}
