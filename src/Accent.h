#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>
namespace island {
// Port of Nezur ArtworkAccent's OKLab color field and gamut-preserving blending.
class ArtworkAccent {
    using Triple=std::array<double,3>;
    static double linear(unsigned c){double v=c/255.;return v<=.04045?v/12.92:std::pow((v+.055)/1.055,2.4);}
    static Triple lab(uint32_t color){
        double r=linear((color>>16)&255),g=linear((color>>8)&255),b=linear(color&255);
        double l=std::cbrt(.4122214708*r+.5363325363*g+.0514459929*b),m=std::cbrt(.2119034982*r+.6806995451*g+.1073969566*b),s=std::cbrt(.0883024619*r+.2817188376*g+.6299787005*b);
        return {.2104542553*l+.7936177850*m-.0040720468*s,1.9779984951*l-2.4285922050*m+.4505937099*s,.0259040371*l+.7827717662*m-.8086757660*s};
    }
    static Triple rgb(double light,double a,double b){double l=light+.3963377774*a+.2158037573*b,m=light-.1055613458*a-.0638541728*b,s=light-.0894841775*a-1.2914855480*b;l=l*l*l;m=m*m*m;s=s*s*s;return {4.0767416621*l-3.3077115913*m+.2309699292*s,-1.2684380046*l+2.6097574011*m-.3413193965*s,-.0041960863*l-.7034186147*m+1.7076147010*s};}
    static bool gamut(const Triple& rgb){for(double c:rgb)if(c<0||c>1)return false;return true;}
    static unsigned encoded(double linear){double v=std::clamp(linear,0.0,1.0);return static_cast<unsigned>(std::round(255*(v<=.0031308?12.92*v:1.055*std::pow(v,1/2.4)-.055)));}
    static uint32_t fromLab(double light,double a,double b){auto channels=rgb(light,a,b);if(!gamut(channels)){double low=0,high=1;for(int i=0;i<10;i++){double mid=(low+high)*.5;if(gamut(rgb(light,a*mid,b*mid)))low=mid;else high=mid;}channels=rgb(light,a*low,b*low);}return 0xff000000|(encoded(channels[0])<<16)|(encoded(channels[1])<<8)|encoded(channels[2]);}
public:
    using Palette=std::array<uint32_t,12>;
    static uint32_t blend(uint32_t first,uint32_t second,double t){if(t<=0)return first;if(t>=1)return second;auto a=lab(first),b=lab(second);return fromLab(a[0]+(b[0]-a[0])*t,a[1]+(b[1]-a[1])*t,a[2]+(b[2]-a[2])*t);}
    static Palette columns(const std::vector<unsigned char>& pixels,unsigned width,unsigned height){
        Palette colors;colors.fill(0xffAAAAAF);if(!width||!height||pixels.size()<static_cast<size_t>(width)*height*4)return colors;
        unsigned size=std::min(width,height),cropX=(width-size)/2,cropY=(height-size)/2,grid=std::min(40u,size);
        std::array<std::array<double,7>,12> sums{};double coverage=0,visible=0;
        for(unsigned row=0;row<grid;row++)for(unsigned col=0;col<grid;col++){
            double x=(col+.5)/grid,y=(row+.5)/grid;unsigned px=cropX+std::min(size-1,static_cast<unsigned>(x*size)),py=cropY+std::min(size-1,static_cast<unsigned>(y*size));auto p=(py*width+px)*4;
            unsigned alphaByte=pixels[p+3];double alpha=alphaByte/255.;if(alpha<.1)continue;
            auto channel=[&](unsigned c){return std::min(255u,pixels[p+c]*255u/alphaByte);};uint32_t color=0xff000000|(channel(2)<<16)|(channel(1)<<8)|channel(0);auto value=lab(color);double chroma=std::hypot(value[1],value[2]),support=std::clamp((chroma-.025)/.06,0.0,1.0);coverage+=alpha*support;visible+=alpha;
            double detail=(.30+.70*std::sin(3.141592653589793*value[0]))*(.70+std::min(1.0,chroma/.20)),vertical=.85+.15*std::exp(-std::pow((y-.5)/.35,2)*.5);
            for(size_t i=0;i<12;i++){double center=(i+.5)/12,weight=alpha*detail*vertical*std::exp(-std::pow((x-center)/.17,2)*.5);sums[i][0]+=weight;for(size_t c=0;c<3;c++)sums[i][c+1]+=value[c]*weight;sums[i][4]+=weight*support;sums[i][5]+=value[1]*weight*support;sums[i][6]+=value[2]*weight*support;}
        }
        for(size_t i=0;i<12;i++){
            if(sums[i][0]<1e-8)continue;double light=sums[i][1]/sums[i][0],a=sums[i][2]/sums[i][0],b=sums[i][3]/sums[i][0];
            if(sums[i][4]>1e-8){double confidence=std::clamp((coverage/std::max(1e-8,visible)-.02)/.16,0.0,1.0)*std::min(1.0,sums[i][4]/sums[i][0]/.18);a+=(sums[i][5]/sums[i][4]-a)*confidence;b+=(sums[i][6]/sums[i][4]-b)*confidence;}
            double chroma=std::hypot(a,b),gain=chroma<1e-8?0:std::min(1.25,.16/chroma);colors[i]=fromLab(.78+.06*(light-.55),a*gain,b*gain);
        }
        return colors;
    }
};
}
