#include "Renderer.h"
#include <shlwapi.h>
#include <filesystem>
#include <stdexcept>

namespace island {
using Microsoft::WRL::ComPtr;
static void check(HRESULT value) {if(FAILED(value))throw std::runtime_error("DirectX operation failed: "+std::to_string(static_cast<unsigned long>(value)));}
Renderer::Renderer(HWND window,float renderingDpi):dpi(renderingDpi) {
    UINT flags=D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL feature;
    HRESULT result=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,flags,nullptr,0,D3D11_SDK_VERSION,&d3d,&feature,&d3dContext);
    if(FAILED(result))check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,flags,nullptr,0,D3D11_SDK_VERSION,&d3d,&feature,&d3dContext));
    ComPtr<IDXGIDevice> dxgi;check(d3d.As(&dxgi));
    D2D1_FACTORY_OPTIONS options{};check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,__uuidof(ID2D1Factory1),&options,reinterpret_cast<void**>(factory.GetAddressOf())));
    check(factory->CreateDevice(dxgi.Get(),&d2d));check(d2d->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,&context));
    context->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);context->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    check(context->CreateSolidColorBrush(D2D1::ColorF(1,1,1),&brush));
    auto stroke=D2D1::StrokeStyleProperties();stroke.startCap=stroke.endCap=stroke.dashCap=D2D1_CAP_STYLE_ROUND;stroke.lineJoin=D2D1_LINE_JOIN_ROUND;
    check(factory->CreateStrokeStyle(stroke,nullptr,0,&roundedStroke));
    check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(write.GetAddressOf())));
    font=L"Segoe UI";ComPtr<IDWriteFontCollection> fonts;check(write->GetSystemFontCollection(&fonts));UINT32 index=0;BOOL exists=FALSE;fonts->FindFamilyName(L"Segoe UI Variable Display",&index,&exists);if(exists)font=L"Segoe UI Variable Display";
    check(CoCreateInstance(CLSID_WICImagingFactory2,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic)));
    ComPtr<IDXGIAdapter> adapter;ComPtr<IDXGIFactory2> dxgiFactory;check(dxgi->GetAdapter(&adapter));check(adapter->GetParent(IID_PPV_ARGS(&dxgiFactory)));
    width=static_cast<unsigned>(std::ceil(CanvasWidth*dpi/96));height=static_cast<unsigned>(std::ceil(CanvasHeight*dpi/96));
    DXGI_SWAP_CHAIN_DESC1 desc{};desc.Width=width;desc.Height=height;desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=2;desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;desc.AlphaMode=DXGI_ALPHA_MODE_PREMULTIPLIED;desc.Scaling=DXGI_SCALING_STRETCH;
    desc.Flags=DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    check(dxgiFactory->CreateSwapChainForComposition(d3d.Get(),&desc,nullptr,&swap));
    ComPtr<IDXGISwapChain2> paced;check(swap.As(&paced));check(paced->SetMaximumFrameLatency(1));
    check(DCompositionCreateDevice(dxgi.Get(),__uuidof(IDCompositionDevice),reinterpret_cast<void**>(composition.GetAddressOf())));
    check(composition->CreateTargetForHwnd(window,TRUE,&target));check(composition->CreateVisual(&visual));check(visual->SetContent(swap.Get()));check(target->SetRoot(visual.Get()));check(composition->Commit());
    createTargets();
    colors.fill(0xffAAAAAF);previousColors=colors;
    frameReady=paced->GetFrameLatencyWaitableObject();if(!frameReady)throw std::runtime_error("Could not create the display frame signal");
}
Renderer::~Renderer() {if(frameReady)CloseHandle(frameReady);}
void Renderer::createTargets() {
    ComPtr<IDXGISurface> surface;check(swap->GetBuffer(0,IID_PPV_ARGS(&surface)));
    auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),dpi,dpi);
    check(context->CreateBitmapFromDxgiSurface(surface.Get(),props,&screen));
    auto large=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),dpi*2,dpi*2);
    check(context->CreateBitmap(D2D1::SizeU(width*2,height*2),nullptr,0,large,&oversampled));context->SetTarget(screen.Get());context->SetDpi(dpi,dpi);
}
void Renderer::resize(float renderingDpi) {
    dpi=renderingDpi;width=static_cast<unsigned>(std::ceil(CanvasWidth*dpi/96));height=static_cast<unsigned>(std::ceil(CanvasHeight*dpi/96));
    context->SetTarget(nullptr);screen.Reset();oversampled.Reset();check(swap->ResizeBuffers(0,width,height,DXGI_FORMAT_UNKNOWN,DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT));createTargets();
}
void Renderer::color(D2D1_COLOR_F value,float opacity) {brush->SetColor(value);brush->SetOpacity(std::clamp(opacity,0.f,1.f));}
void Renderer::text(const std::wstring& value,float x,float y,float size,float available,float opacity,bool semibold,double offset,Marquee* marquee) {
    if(value.empty()||available<=0||opacity<.001f)return;
    auto limited=value.substr(0,512);ComPtr<IDWriteTextFormat> format;
    check(write->CreateTextFormat(font.c_str(),nullptr,semibold?DWRITE_FONT_WEIGHT_SEMI_BOLD:DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,size,L"en-us",&format));
    ComPtr<IDWriteTextLayout> layout;check(write->CreateTextLayout(limited.c_str(),static_cast<UINT32>(limited.size()),format.Get(),4096,size*2,&layout));
    DWRITE_TEXT_METRICS metrics{};layout->GetMetrics(&metrics);
    // Scroll in a fixed font coordinate system so resizing retains the same glyph.
    if(marquee)offset=marquee->update(textNow,textDt,(metrics.widthIncludingTrailingWhitespace-available)*12/size,textMoving)*size/12;
    context->PushAxisAlignedClip(D2D1::RectF(x,y,x+available,y+size*1.65f),D2D1_ANTIALIAS_MODE_ALIASED);
    color(D2D1::ColorF(1,1,1),opacity);context->DrawTextLayout(D2D1::Point2F(x-static_cast<float>(offset),y),layout.Get(),brush.Get(),D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
    context->PopAxisAlignedClip();
}
void Renderer::polygon(const std::array<D2D1_POINT_2F,4>& points,float rounding,float opacity) {
    if(opacity<.001f)return;
    ComPtr<ID2D1PathGeometry> geometry;ComPtr<ID2D1GeometrySink> sink;check(factory->CreatePathGeometry(&geometry));check(geometry->Open(&sink));
    auto blend=[](D2D1_POINT_2F a,D2D1_POINT_2F b,float t){return D2D1::Point2F(a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t);};
    sink->BeginFigure(blend(points[0],points[3],rounding),D2D1_FIGURE_BEGIN_FILLED);
    for(int i=0;i<4;i++) {
        auto before=blend(points[i],points[(i+3)%4],rounding),after=blend(points[i],points[(i+1)%4],rounding);
        if(i>0)sink->AddLine(before);
        auto a=blend(before,points[i],2.f/3),b=blend(after,points[i],2.f/3);sink->AddBezier(D2D1::BezierSegment(a,b,after));
    }
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);check(sink->Close());color(D2D1::ColorF(1,1,1),opacity);context->FillGeometry(geometry.Get(),brush.Get());
}
void Renderer::bitmap(ID2D1Bitmap1* value,D2D1_RECT_F rect,float opacity) {
    if(!value) {
        color(D2D1::ColorF(.13f,.13f,.15f),opacity);context->FillRectangle(rect,brush.Get());float size=rect.right-rect.left;
        color(D2D1::ColorF(.64f,.64f,.67f),opacity);context->DrawLine(D2D1::Point2F(rect.left+size*.62f,rect.top+size*.25f),D2D1::Point2F(rect.left+size*.62f,rect.top+size*.70f),brush.Get(),size*.05f,roundedStroke.Get());
        context->DrawLine(D2D1::Point2F(rect.left+size*.62f,rect.top+size*.25f),D2D1::Point2F(rect.left+size*.79f,rect.top+size*.32f),brush.Get(),size*.06f,roundedStroke.Get());
        context->FillEllipse(D2D1::Ellipse(D2D1::Point2F(rect.left+size*.51f,rect.top+size*.70f),size*.12f,size*.09f),brush.Get());return;
    }
    auto size=value->GetSize();float side=std::min(size.width,size.height);
    auto source=D2D1::RectF((size.width-side)*.5f,(size.height-side)*.5f,(size.width+side)*.5f,(size.height+side)*.5f);
    context->DrawBitmap(value,rect,opacity,D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC,source,nullptr);
}
void Renderer::cover(D2D1_RECT_F rect,float radius,double now,bool reduced) {
    float t=static_cast<float>(smooth((now-artBegan)/(reduced?.16:CoverFade)));
    ComPtr<ID2D1RoundedRectangleGeometry> clip;check(factory->CreateRoundedRectangleGeometry(D2D1::RoundedRect(rect,radius,radius),&clip));
    context->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(),clip.Get()),nullptr);
    color(D2D1::ColorF(0,0,0));context->FillRectangle(rect,brush.Get());
    if(previousArt&&t<1) {
        bitmap(previousArt.Get(),rect,1);
    } else if(t<1)bitmap(nullptr,rect,1);
    bitmap(art.Get(),rect,t);context->PopLayer();
}
void Renderer::loadArt(const Snapshot& s,double now) {
    if(s.available&&art&&!s.artwork&&now-titleBegan<1.5)return;
    if(s.artHash==artHash)return;
    double paletteMix=smooth((now-artBegan)/CoverFade);for(size_t i=0;i<12;i++)previousColors[i]=ArtworkAccent::blend(previousColors[i],colors[i],paletteMix);colors.fill(0xffAAAAAF);
    ComPtr<ID2D1Bitmap1> visible;
    if(previousArt&&now-artBegan<CoverFade) {
        auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
        check(context->CreateBitmap(D2D1::SizeU(512,512),nullptr,0,props,&visible));ComPtr<ID2D1Image> saved;context->GetTarget(&saved);auto savedMatrix=matrix();float x,y;context->GetDpi(&x,&y);
        context->SetTarget(visible.Get());context->SetDpi(96,96);context->SetTransform(D2D1::Matrix3x2F::Identity());context->BeginDraw();context->Clear(D2D1::ColorF(0,0));cover(D2D1::RectF(0,0,512,512),0,now,false);check(context->EndDraw());context->SetTarget(saved.Get());context->SetDpi(x,y);context->SetTransform(savedMatrix);
    }
    previousArt=visible?visible:art;art.Reset();artHash=s.artHash;artBegan=previousArt?now:now-1;
    if(!s.artwork||s.artwork->empty())return;
    ComPtr<IStream> stream;stream.Attach(SHCreateMemStream(s.artwork->data(),static_cast<UINT>(s.artwork->size())));if(!stream)return;
    ComPtr<IWICBitmapDecoder> decoder;ComPtr<IWICBitmapFrameDecode> frame;ComPtr<IWICFormatConverter> converter;
    if(FAILED(wic->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnLoad,&decoder))||FAILED(decoder->GetFrame(0,&frame)))return;
    UINT w=0,h=0;frame->GetSize(&w,&h);if(!w||!h||w>4096||h>4096)return;
    ComPtr<IWICBitmapScaler> scaler;check(wic->CreateBitmapScaler(&scaler));float factor=std::min(1.f,1024.f/std::max(w,h));check(scaler->Initialize(frame.Get(),std::max(1u,static_cast<UINT>(w*factor)),std::max(1u,static_cast<UINT>(h*factor)),WICBitmapInterpolationModeFant));
    check(wic->CreateFormatConverter(&converter));check(converter->Initialize(scaler.Get(),GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));check(context->CreateBitmapFromWicBitmap(converter.Get(),nullptr,&art));
    UINT cw,ch;converter->GetSize(&cw,&ch);std::vector<unsigned char> pixels(cw*ch*4);check(converter->CopyPixels(nullptr,cw*4,static_cast<UINT>(pixels.size()),pixels.data()));
    colors=ArtworkAccent::columns(pixels,cw,ch);
}
void Renderer::play(float x,float y,double shape,double press,float opacity) {
    auto transform=matrix();float p=static_cast<float>(1-.075*clamp01(press));context->SetTransform(D2D1::Matrix3x2F::Scale(p,p,D2D1::Point2F(x,y))*transform);
    float t=static_cast<float>(clamp01(shape)),s=2.1f;
    auto point=[&](float ax,float ay,float bx,float by){return D2D1::Point2F(x+s*static_cast<float>(mix(ax,bx,t)),y+s*static_cast<float>(mix(ay,by,t)));};
    polygon({point(-4,-6.1f,-4.7f,-6.1f),point(1.3f,-3.1f,-1.3f,-6.1f),point(1.3f,3.1f,-1.3f,6.1f),point(-4,6.1f,-4.7f,6.1f)},.06f,opacity);
    polygon({point(1.3f,-3.1f,1.3f,-6.1f),point(6.1f,0,4.7f,-6.1f),point(6.1f,0,4.7f,6.1f),point(1.3f,3.1f,1.3f,6.1f)},.06f,opacity);context->SetTransform(transform);
}
void Renderer::skip(float x,float y,int direction,double phase,double press,float opacity) {
    auto transform=matrix();float p=static_cast<float>(1-.1*clamp01(press));context->SetTransform(D2D1::Matrix3x2F::Scale(p,p,D2D1::Point2F(x,y))*transform);
    context->PushAxisAlignedClip(D2D1::RectF(x-16,y-11,x+16,y+11),D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    float t=static_cast<float>(smooth(phase)),shift=7.2f*t;
    auto triangle=[&](float offset,float a){auto point=[&](float px,float py){return D2D1::Point2F(x+direction*(px+offset)*1.65f,y+py*1.65f);};polygon({point(-7,-4.1f),point(-.3f,0),point(-7,4.1f),point(-7,4.1f)},.08f,a*opacity);};
    triangle(shift,1);triangle(7.2f+shift,1-t);triangle(shift-7.2f,t);context->PopAxisAlignedClip();context->SetTransform(transform);
}
void Renderer::edgeHandle(double amount) {
    if(amount<.001)return;color(D2D1::ColorF(.68f,.68f,.70f),static_cast<float>(.22*clamp01(amount)));
    context->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(CanvasWidth/2-24,0,CanvasWidth/2+24,3),1.5f,1.5f),brush.Get());
}
void Renderer::outputScale(float factor) {if(std::abs(factor-outputFactor)<.000001f)return;check(visual->SetTransform(D2D1::Matrix3x2F::Scale(factor,factor)));check(composition->Commit());outputFactor=factor;}
void Renderer::render(Motion& m,const Snapshot& s,double now,double elapsed,bool playing,bool preview,int /*direction*/,bool present) {
    if(m.presence.hidden()){
        lastFrame=now;context->SetTarget(screen.Get());context->SetDpi(dpi,dpi);context->SetTransform(D2D1::Matrix3x2F::Identity());context->BeginDraw();context->Clear(D2D1::ColorF(0,0));edgeHandle(m.peek.value);check(context->EndDraw());
        if(present){HRESULT hr=swap->Present(1,0);if(hr!=DXGI_STATUS_OCCLUDED)check(hr);}return;
    }
    if(s.track!=track){oldTitle=title;oldArtist=artist;oldTitleOffset=titleScroll.value();oldArtistOffset=artistScroll.value();titleScroll.restart(now,true);artistScroll.restart(now,true);track=s.track;title=s.available?(s.title.empty()?L"Unknown track":s.title):L"MusicIsland";artist=s.artist.empty()?s.source:s.artist;titleBegan=now;}
    if(title.empty()){title=s.available?s.title:L"MusicIsland";artist=s.artist;}
    loadArt(s,now);
    double dt=lastFrame?std::clamp(now-lastFrame,0.0,.1):1.0/60;lastFrame=now;
    textNow=now;textDt=dt;textMoving=m.width.moving()||m.height.moving();
    meter.step(s.peaks,dt,playing&&s.available&&s.source==s.audioSource&&now-s.audioReceived>=0&&now-s.audioReceived<.35,s.source);
    context->SetTarget(oversampled.Get());context->SetDpi(dpi*2,dpi*2);context->SetTransform(D2D1::Matrix3x2F::Identity());context->BeginDraw();context->Clear(D2D1::ColorF(0,0));
    float w=static_cast<float>(m.width.value),h=static_cast<float>(m.height.value),r=std::min(static_cast<float>(m.radius.value),h*.5f),left=(CanvasWidth-w)*.5f,top=BodyTop+static_cast<float>(m.verticalOffset());
    auto bounds=m.presence.bounds(w,h,r,top);float visibleLeft=static_cast<float>((CanvasWidth-bounds.width)/2),visibleTop=static_cast<float>(bounds.top),visibleWidth=static_cast<float>(bounds.width),visibleHeight=static_cast<float>(bounds.height),visibleRadius=static_cast<float>(bounds.radius);
    float pressScale=static_cast<float>(m.pressScale());context->SetTransform(D2D1::Matrix3x2F::Scale(pressScale,pressScale,D2D1::Point2F(CanvasWidth/2,visibleTop+visibleHeight/2)));
    for(int i=12;i>=1;i--){float margin=i*.6f;auto shadow=D2D1::RectF(visibleLeft-margin,visibleTop-margin+2,visibleLeft+visibleWidth+margin,visibleTop+visibleHeight+margin+2);color(D2D1::ColorF(0,0,0),.012f);context->FillRoundedRectangle(D2D1::RoundedRect(shadow,visibleRadius+margin,visibleRadius+margin),brush.Get());}
    auto body=D2D1::RectF(visibleLeft,visibleTop,visibleLeft+visibleWidth,visibleTop+visibleHeight);color(D2D1::ColorF(0,0,0));context->FillRoundedRectangle(D2D1::RoundedRect(body,visibleRadius,visibleRadius),brush.Get());
    ComPtr<ID2D1RoundedRectangleGeometry> bodyClip;check(factory->CreateRoundedRectangleGeometry(D2D1::RoundedRect(body,visibleRadius,visibleRadius),&bodyClip));auto contentLayer=D2D1::LayerParameters1(D2D1::InfiniteRect(),bodyClip.Get());contentLayer.opacity=static_cast<float>(m.presence.content());context->PushLayer(contentLayer,nullptr);
    float expansion=static_cast<float>(clamp01((h-36)/156));float reveal=static_cast<float>(smooth((expansion-.35)/.65));
    float size=static_cast<float>(m.cover.value),cx=left+static_cast<float>(m.coverX.value),cy=top+static_cast<float>(m.coverY.value);
    float breathing=static_cast<float>(mix(1,mix(.92,1,clamp01(m.playGlyph.value)),expansion));auto savedMatrix=matrix();context->SetTransform(D2D1::Matrix3x2F::Scale(breathing,breathing,D2D1::Point2F(cx+size/2,cy+size/2))*savedMatrix);cover(D2D1::RectF(cx,cy,cx+size,cy+size),size*.20f,now,m.reduced);context->SetTransform(savedMatrix);
    auto wave=waveformLayout(expansion);
    float mx=left+w-wave.rightInset-wave.width(),my=top+wave.centerY;
    float titleX=left+static_cast<float>(mix(40,102,expansion)),titleY=top+static_cast<float>(mix(9,28,expansion)),fontSize=static_cast<float>(mix(12,19,expansion)),available=std::max(0.f,mx-12-titleX);
    float progress=static_cast<float>(clamp01((now-titleBegan)/(m.reduced?.16:.32)));
    float outgoing=static_cast<float>(1-smooth(progress*2)),incoming=static_cast<float>(smooth((progress-.5)*2));
    if(!oldTitle.empty()&&outgoing>0)text(oldTitle,titleX,titleY,fontSize,available,outgoing,true,oldTitleOffset*fontSize/12);
    text(title,titleX,titleY,fontSize,available,incoming,true,0,&titleScroll);
    if(reveal>.001f){if(!oldArtist.empty()&&outgoing>0)text(oldArtist,titleX,titleY+28,13.5f,available,.48f*outgoing*reveal,false,oldArtistOffset*13.5/12);text(artist,titleX,titleY+28,13.5f,available,.55f*incoming*reveal,false,0,&artistScroll);}
    double paletteMix=smooth((now-artBegan)/(m.reduced?.16:CoverFade));
    for(size_t i=0;i<VisualizerMotion::Bars;i++){
        float bar=wave.height(meter.level(i)),stroke=wave.stroke,x=mx+static_cast<float>(i)*wave.pitch;
        auto asColor=[](uint32_t c){return D2D1::ColorF(((c>>16)&255)/255.f,((c>>8)&255)/255.f,(c&255)/255.f);};
        auto leftColor=ArtworkAccent::blend(previousColors[i*2],colors[i*2],paletteMix),rightColor=ArtworkAccent::blend(previousColors[i*2+1],colors[i*2+1],paletteMix);
        if(!waveBrushes[i]||waveLeft[i]!=leftColor||waveRight[i]!=rightColor){D2D1_GRADIENT_STOP stops[]={{0,asColor(leftColor)},{1,asColor(rightColor)}};ComPtr<ID2D1GradientStopCollection> gradient;check(context->CreateGradientStopCollection(stops,2,D2D1_GAMMA_1_0,D2D1_EXTEND_MODE_CLAMP,&gradient));waveBrushes[i].Reset();check(context->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(x,my),D2D1::Point2F(x+stroke,my)),gradient.Get(),&waveBrushes[i]));waveLeft[i]=leftColor;waveRight[i]=rightColor;}
        waveBrushes[i]->SetStartPoint(D2D1::Point2F(x,my));waveBrushes[i]->SetEndPoint(D2D1::Point2F(x+stroke,my));context->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x,my-bar/2,x+stroke,my+bar/2),stroke/2,stroke/2),waveBrushes[i].Get());
    }
    if(reveal>.001f) {
        float seek=static_cast<float>(clamp01(m.seek.value));auto seekBounds=seekBar(left,w,seek);float bx=static_cast<float>(seekBounds.left),bw=static_cast<float>(seekBounds.width),by=top+108,thickness=static_cast<float>(seekBounds.thickness);
        auto bar=D2D1::RoundedRect(D2D1::RectF(bx,by-thickness/2,bx+bw,by+thickness/2),thickness/2,thickness/2);color(D2D1::ColorF(.17f,.17f,.19f),reveal);context->FillRoundedRectangle(bar,brush.Get());
        double fraction=progressMotion.update(s.duration>0?elapsed/s.duration:0,s.session+L"\n"+s.track,dt,m.reduced,m.seek.target>0);
        if(fraction>0){auto fill=bar;fill.rect.right=bx+bw*static_cast<float>(fraction);fill.radiusX=fill.radiusY=std::min(thickness/2,(fill.rect.right-bx)/2);color(D2D1::ColorF(1,1,1),reveal);context->FillRoundedRectangle(fill,brush.Get());}
        if(seek>.001f){float head=bx+bw*static_cast<float>(fraction),feedback=static_cast<float>(smooth(seek)),headRadius=thickness/2+1.5f*seek;color(D2D1::ColorF(1,1,1),reveal*.10f*feedback);context->FillEllipse(D2D1::Ellipse(D2D1::Point2F(head,by),headRadius+2.5f,headRadius+2.5f),brush.Get());color(D2D1::ColorF(1,1,1),reveal*feedback);context->FillEllipse(D2D1::Ellipse(D2D1::Point2F(head,by),headRadius,headRadius),brush.Get());}
        text(timeLabel(elapsed),left+28,top+119,11,70,reveal*.48f);text(s.duration>0?L"-"+timeLabel(s.duration-elapsed):L"--:--",left+w-65,top+119,11,48,reveal*.48f);
        float mid=CanvasWidth/2,controlsY=top+158;
        skip(mid-70,controlsY,-1,m.reduced?1:m.previous.phase(now),m.previousPress.value,reveal*(s.previous?1:.27f));
        play(mid,controlsY,m.playGlyph.value,m.playPress.value,reveal);
        skip(mid+70,controlsY,1,m.reduced?1:m.next.phase(now),m.nextPress.value,reveal*(s.next?1:.27f));
        if(preview)text(L"PREVIEW",left+28,top+174,8,70,.35f*reveal);
        else if(!s.error.empty())text(s.error,left+28,top+174,8,w-56,.5f*reveal);
        else if(!s.available)text(L"Play music in a media app",left+28,top+174,9,w-56,.5f*reveal);
    }
    context->PopLayer();check(context->EndDraw());
    context->SetTarget(screen.Get());context->SetDpi(dpi,dpi);context->SetTransform(D2D1::Matrix3x2F::Identity());context->BeginDraw();context->Clear(D2D1::ColorF(0,0));
    context->DrawBitmap(oversampled.Get(),D2D1::RectF(0,0,CanvasWidth,CanvasHeight),static_cast<float>(m.presence.opacity()),D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC,nullptr,nullptr);edgeHandle(m.peek.value);check(context->EndDraw());
    if(present){HRESULT hr=swap->Present(1,0);if(hr!=DXGI_STATUS_OCCLUDED)check(hr);}
    if(now-artBegan>=CoverFade)previousArt.Reset();
}
void Renderer::save(const std::wstring& path) {
    ComPtr<ID3D11Texture2D> buffer,staging;check(swap->GetBuffer(0,IID_PPV_ARGS(&buffer)));D3D11_TEXTURE2D_DESC desc{};buffer->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;check(d3d->CreateTexture2D(&desc,nullptr,&staging));d3dContext->CopyResource(staging.Get(),buffer.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};check(d3dContext->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped));
    ComPtr<IWICStream> stream;ComPtr<IWICBitmapEncoder> encoder;ComPtr<IWICBitmapFrameEncode> frame;ComPtr<IPropertyBag2> options;
    check(wic->CreateStream(&stream));check(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE));check(wic->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder));check(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));check(encoder->CreateNewFrame(&frame,&options));check(frame->Initialize(options.Get()));check(frame->SetSize(width,height));WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;check(frame->SetPixelFormat(&format));
    // Screenshots need straight alpha; the composition back buffer uses premultiplied pixels.
    std::vector<unsigned char> pixels(width*height*4);for(unsigned y=0;y<height;y++)for(unsigned x=0;x<width;x++){auto src=static_cast<unsigned char*>(mapped.pData)+y*mapped.RowPitch+x*4;auto dst=pixels.data()+(y*width+x)*4;dst[3]=src[3];for(int c=0;c<3;c++)dst[c]=src[3]?static_cast<unsigned char>(std::min(255,src[c]*255/src[3])):0;}
    d3dContext->Unmap(staging.Get(),0);check(frame->WritePixels(height,width*4,static_cast<UINT>(pixels.size()),pixels.data()));check(frame->Commit());check(encoder->Commit());
}
}
