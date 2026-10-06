#pragma once
#include "Motion.h"
#include "Media.h"
#include "TextMotion.h"
#include "Accent.h"
#include <d2d1_1.h>
#include <d2d1_1helper.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <dcomp.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <array>

namespace island {
inline constexpr float CanvasWidth=440,CanvasHeight=264,BodyTop=18;
class Renderer {
    template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
    Ptr<ID3D11Device> d3d;
    Ptr<ID3D11DeviceContext> d3dContext;
    Ptr<IDXGISwapChain1> swap;
    HANDLE frameReady=nullptr;
    Ptr<ID2D1Factory1> factory;
    Ptr<ID2D1Device> d2d;
    Ptr<ID2D1DeviceContext> context;
    Ptr<ID2D1Bitmap1> screen,oversampled,art,previousArt;
    Ptr<IDCompositionDevice> composition;
    Ptr<IDCompositionTarget> target;
    Ptr<IDCompositionVisual> visual;
    Ptr<IDWriteFactory> write;
    Ptr<IWICImagingFactory> wic;
    Ptr<ID2D1SolidColorBrush> brush;
    Ptr<ID2D1StrokeStyle> roundedStroke;
    std::wstring font,track,title,artist,oldTitle,oldArtist;
    unsigned long long artHash=0;
    double artBegan=-10,titleBegan=-10,lastFrame=0;
    double textNow=0,textDt=0,oldTitleOffset=0,oldArtistOffset=0;
    bool textMoving=false;
    Marquee titleScroll,artistScroll;
    static constexpr double CoverFade=.42;
    float dpi=96;
    float outputFactor=1;
    unsigned width=440,height=264;
    VisualizerMotion meter;
    ProgressMotion progressMotion;
    ArtworkAccent::Palette colors,previousColors;
    std::array<Ptr<ID2D1LinearGradientBrush>,6> waveBrushes;
    std::array<uint32_t,6> waveLeft{},waveRight{};
    void createTargets();
    D2D1::Matrix3x2F matrix() const {D2D1::Matrix3x2F value;context->GetTransform(&value);return value;}
    void color(D2D1_COLOR_F value,float opacity=1);
    void text(const std::wstring& value,float x,float y,float size,float available,float opacity,bool semibold=false,double offset=0,Marquee* marquee=nullptr);
    void polygon(const std::array<D2D1_POINT_2F,4>& vertices,float radius,float opacity);
    void cover(D2D1_RECT_F rect,float radius,double now,bool reduced);
    void bitmap(ID2D1Bitmap1* value,D2D1_RECT_F rect,float opacity);
    void loadArt(const Snapshot& s,double now);
    void play(float x,float y,double shape,double press,float opacity);
    void skip(float x,float y,int direction,double phase,double press,float opacity);
    void edgeHandle(double amount);
public:
    explicit Renderer(HWND window,float renderingDpi);
    ~Renderer();
    HANDLE frameWait() const {return frameReady;}
    void resize(float renderingDpi);
    void outputScale(float factor);
    void render(Motion& motion,const Snapshot& snapshot,double now,double elapsed,bool playing,bool preview,int direction,bool present=true);
    void save(const std::wstring& path);
    float renderingDpi() const {return dpi;}
};
}
