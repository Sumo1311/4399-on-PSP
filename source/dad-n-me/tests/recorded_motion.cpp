#include "../src/recorded_renderer.hpp"
#include "null_renderer.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
using namespace gameswf;
static unsigned checks=0,endpoint_cases=0,endpoint_errors=0,fallback_cases=0;
static void check(bool ok){++checks;if(!ok){std::fprintf(stderr,"recorded motion check %u failed\n",checks);std::exit(1);}}
static bool same(const matrix& a,const matrix& b){return std::memcmp(a.m_,b.m_,sizeof(a.m_))==0;}
static matrix legacy(const matrix& current,const matrix& previous,const matrix& draw){
 matrix motion;
 if(!(std::fabs(current.get_determinant())<1e-12f)){matrix inverse;inverse.set_inverse(current);motion=previous;motion.concatenate(inverse);}
 motion.concatenate(draw);return motion;
}
struct Resources:NullRenderer {bool has_baked_shape(int,Uint16)const override{return true;}};
static void no_lock(){}
static uint32_t random_state=0x519db801;
static uint32_t next(){random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return random_state;}
static matrix sample(){
 matrix m;
 for(int r=0;r<2;++r)for(int c=0;c<3;++c)m.m_[r][c]=float(int(next()%20001)-10000)/64.f;
 return m;
}
int main(int argc,char** argv){
 bool observe=argc==2&&std::strcmp(argv[1],"--observe")==0;
 Resources resources;psp60::Recorder recorder(&resources,no_lock,no_lock);psp60::Frame frame;cxform color;
 for(unsigned i=0;i<6000;++i){
  matrix current=sample(),previous=i%2?sample():current;
  if(i%137==0){for(int r=0;r<2;++r)for(int c=0;c<2;++c)current.m_[r][c]=0;}
  if(i%139==0){current.m_[0][0]=1e-7f;current.m_[0][1]=0;current.m_[1][0]=0;current.m_[1][1]=1e-7f;}
  if(i%149==0){current.m_[0][0]=1e30f;current.m_[1][1]=1e30f;}
  const float determinant=current.get_determinant();bool direct=std::isfinite(determinant)&&std::fabs(determinant)>=1e-12f;
  recorder.record(frame);recorder.set_motion(current,previous);check(recorder.draw_shape(1,current,color,0));check(frame.commands.size()==1);check(same(frame.commands[0].m,current));
  if(direct){++endpoint_cases;endpoint_errors+=!same(frame.commands[0].previous,previous);}
  else{check(same(frame.commands[0].previous,legacy(current,previous,current)));++fallback_cases;}
  check(same(psp60::Frame::blend(frame.commands[0],1.f),current));
  // Additional drawing-local transforms retain the established fallback.
  matrix local;local.m_[0][2]=17;local.m_[1][2]=-9;matrix draw=current;draw.concatenate(local);
  if(!same(draw,current)){
   check(recorder.draw_shape(2,draw,color,0));check(same(frame.commands.back().previous,legacy(current,previous,draw)));++fallback_cases;
   recorder.set_matrix(draw);check(same(frame.commands.back().previous,legacy(current,previous,draw)));++fallback_cases;
  }
  // A second hint must supersede any cached fallback from the first one.
  matrix other=sample();recorder.set_motion(other,previous);recorder.set_matrix(current);
  if(!same(other,current)){check(same(frame.commands.back().previous,legacy(other,previous,current)));++fallback_cases;}
  recorder.finish();
  // A fresh recording cannot inherit the preceding object's motion hint.
  recorder.record(frame);recorder.set_matrix(current);matrix identity;check(same(frame.commands[0].previous,legacy(identity,identity,current)));recorder.finish();
 }
 std::printf("{\"checks\":%u,\"source_endpoint_cases\":%u,\"source_endpoint_mismatches\":%u,\"fallback_cases\":%u,\"endpoint_invariant_pass\":%s,\"pass\":%s}\n",checks,endpoint_cases,endpoint_errors,fallback_cases,endpoint_errors?"false":"true",endpoint_errors?"false":"true");
 return endpoint_errors&&!observe?1:0;
}
