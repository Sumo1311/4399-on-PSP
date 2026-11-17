#include "../src/baked_bounds_cache.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace gameswf;
static unsigned checks=0;
static void check(bool ok){++checks;if(!ok){std::fprintf(stderr,"bounds mismatch at %u\n",checks);std::exit(1);}}
int main(){
 psp60::BakedBoundsCache cache;unsigned rng=0x571e93;
 auto number=[&](){rng=rng*1664525u+1013904223u;return float(int(rng>>8)%20001-10000)/37;};
 auto test=[&](int id,unsigned ratio,const rect& b,const matrix& m){
  auto expected=b;rect actual;m.transform(&expected);cache.transform(id,ratio,b,m,actual);
  const float a[]={actual.m_x_min,actual.m_x_max,actual.m_y_min,actual.m_y_max},e[]={expected.m_x_min,expected.m_x_max,expected.m_y_min,expected.m_y_max};
  for(int n=0;n<4;++n)check(a[n]==e[n]||(std::isnan(a[n])&&std::isnan(e[n])));
 };
 for(unsigned n=0;n<10000;++n){
  rect b;b.m_x_min=number();b.m_y_min=number();b.m_x_max=b.m_x_min+std::fabs(number());b.m_y_max=b.m_y_min+std::fabs(number());
  matrix m;for(int r=0;r<2;++r)for(int c=0;c<3;++c)m.m_[r][c]=number();
  unsigned id=n%2105,ratio=n%7?0:32768;test(id,ratio,b,m);
  for(unsigned move=0;move<4;++move){m.m_[0][2]=number()*100;m.m_[1][2]=number()*100;test(id,ratio,b,m);}
  b.m_x_max+=10;test(id,ratio,b,m);test(id+128,ratio,b,m);test(id,ratio+1,b,m);
 }
 rect b;b.m_x_min=b.m_y_min=0;b.m_x_max=b.m_y_max=100;matrix m;
 m.m_[0][0]=0;m.m_[1][1]=-0.0f;test(1,0,b,m);test(1,0,b,m);
 m.set_identity();m.m_[0][2]=std::numeric_limits<float>::infinity();test(2,0,b,m);test(2,0,b,m);
 m.m_[0][0]=std::numeric_limits<float>::quiet_NaN();test(3,0,b,m);test(3,0,b,m);
 m.set_identity();m.m_[0][0]=std::numeric_limits<float>::max();test(4,0,b,m);test(4,0,b,m);
 check(cache.hits>30000);check(cache.misses>10000);
 std::printf("{\"checks\":%u,\"hits\":%u,\"misses\":%u,\"pass\":true}\n",checks,cache.hits,cache.misses);
}
