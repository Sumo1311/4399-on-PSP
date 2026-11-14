#pragma once
#include <algorithm>
#include <cmath>
namespace aow { namespace render {
struct Bounds{float l,t,r,b;};
struct Point{float x,y,u,v;};
inline bool rectangle(float& x0,float& y0,float& x1,float& y1,float& u0,float& v0,float& u1,float& v1,Bounds b){
 if(!std::isfinite(x0)||!std::isfinite(y0)||!std::isfinite(x1)||!std::isfinite(y1)||!std::isfinite(u0)||!std::isfinite(v0)||!std::isfinite(u1)||!std::isfinite(v1))return false;
 if(!(x1>x0&&y1>y0))return false;
 float l=std::max(x0,b.l),r=std::min(x1,b.r),t=std::max(y0,b.t),bottom=std::min(y1,b.b);
 if(!(r>l&&bottom>t))return false;
 float du=(u1-u0)/(x1-x0),dv=(v1-v0)/(y1-y0);
 u1=u0+(r-x0)*du;v1=v0+(bottom-y0)*dv;u0+=(l-x0)*du;v0+=(t-y0)*dv;
 x0=l;x1=r;y0=t;y1=bottom;return true;
}
inline int polygon(const Point* input,int n,Point* output,Bounds b){
 Point a[12],next[12];if(n<3||n>8)return 0;
 for(int i=0;i<n;i++){if(!std::isfinite(input[i].x)||!std::isfinite(input[i].y)||!std::isfinite(input[i].u)||!std::isfinite(input[i].v))return 0;a[i]=input[i];}
 for(int side=0;side<4&&n;side++){
  auto distance=[&](Point p){return side==0?p.x-b.l:side==1?b.r-p.x:side==2?p.y-b.t:b.b-p.y;};
  int count=0;Point previous=a[n-1];float pd=distance(previous);
  for(int i=0;i<n;i++){Point current=a[i];float cd=distance(current);
   if((pd>=0)!=(cd>=0)){float t=pd/(pd-cd);Point p{previous.x+(current.x-previous.x)*t,previous.y+(current.y-previous.y)*t,previous.u+(current.u-previous.u)*t,previous.v+(current.v-previous.v)*t};
    // Keep generated screen coordinates on the exact edge despite FP error.
    if(side==0)p.x=b.l;else if(side==1)p.x=b.r;else if(side==2)p.y=b.t;else p.y=b.b;if(count>=12)return 0;next[count++]=p;}
   if(cd>=0){if(count>=12)return 0;next[count++]=current;}previous=current;pd=cd;
  }
  n=count;std::copy(next,next+n,a);
 }
 if(n<3)return 0;std::copy(a,a+n,output);return n;
}
} }
