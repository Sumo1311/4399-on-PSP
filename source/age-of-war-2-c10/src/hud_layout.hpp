#pragma once
#include <cmath>
#include <limits>
#include <vector>
namespace aow { namespace hud {
struct Rect {float x,y,w,h;};
inline Rect primary(int i,int n){return {float((480-(n*32+(n-1)*4))/2+i*36),12,32,32};}
inline Rect upgrade(int i){return {float(56+25*i),49,22,22};}
inline Rect menu(int i,int n){return {194,float(n==5?78+i*34:116+i*30),224,float(n==5?30:26)};}
inline Rect pause(int i){return {154,float(61+i*26),172,24};}
inline Rect returning(){return {180,224,120,28};}
template<class A> int navigate(const std::vector<A>& a,int at,int dx,int dy){
 if(a.empty())return 0;if(at<0||at>=int(a.size()))at=0;
 float x=a[at].x+a[at].w/2,y=a[at].y+a[at].h/2;
 for(int pass=0;pass<2;++pass){int best=at;float score=std::numeric_limits<float>::max();
 for(int i=0;i<int(a.size());++i){if(i==at)continue;float xx=a[i].x+a[i].w/2-x,yy=a[i].y+a[i].h/2-y;
 float f=xx*dx+yy*dy,s=std::fabs(xx*dy-yy*dx);if((pass==0&&f<=0)||(pass==1&&f>=0))continue;
 float v=s*10000+(pass==0?f:f+1000);if(v<score){score=v;best=i;}}
 if(best!=at)return best;}return at;
}
} }
