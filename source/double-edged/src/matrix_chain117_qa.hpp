#pragma once
#include "matrix_chain117.hpp"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
static inline bool de_matrix_qa117(const char* path){
 uint64_t seed=0x753b21e9c8d469a1ULL,checksum=0x1917;unsigned chains=0,components=0,nans=0,twips=0,failures=0;double m[64][6];
 auto random=[&](){seed^=seed<<13;seed^=seed>>7;seed^=seed<<17;return seed;};
 auto bits=[](double x){uint64_t b;std::memcpy(&b,&x,8);return b;};
 auto bitsf=[](float x){uint32_t b;std::memcpy(&b,&x,4);return b;};
 for(unsigned i=0;i<8192;i++){int32_t n=i<64?(i&1?1:-1)*(16777216+int(i/2)-16):(int32_t)random();volatile double coordinate=(double)n/20;float old=(float)(coordinate*20);if(bitsf(old)!=bitsf(de_chain_input117(n)))++failures;++twips;}
 const double special[]={0.,-0.,.025,-.025,.075,-.075,16777215./20,16777216./20,16777217./20,-16777217./20,2147483647./20,2147483648./20,-2147483648./20,-2147483649./20,INFINITY,-INFINITY,NAN};
 for(unsigned i=0;i<1024;i++){
  unsigned count=1+random()%(i<768?12:64);
  for(unsigned k=0;k<count;k++)for(unsigned j=0;j<6;j++){
   if(i<768)m[k][j]=j<4?(int32_t)(random()%20001-10000)/4096.:(int32_t)(random()%10000001-5000000)/20.;
   else m[k][j]=special[random()%(sizeof(special)/sizeof(*special))];
  }
  double original[6],next[6],actual[6];for(unsigned j=0;j<6;j++)original[j]=m[0][j];
  for(unsigned k=1;k<count;k++){de_matrix_values(m[k],original,next);for(unsigned j=0;j<6;j++)original[j]=next[j];}
  de_matrix_chain117(m,count,actual);
  for(unsigned j=0;j<6;j++){++components;if(std::isnan(original[j])&&std::isnan(actual[j])){++nans;continue;}if(bits(original[j])!=bits(actual[j]))++failures;checksum^=bits(actual[j]);checksum*=0x100000001b3ULL;}++chains;
 }
 FILE*f=std::fopen(path,"w");if(!f)return false;std::fprintf(f,"{\"passed\":%s,\"chains\":%u,\"components\":%u,\"nan_components\":%u,\"twip_inputs\":%u,\"failures\":%u,\"checksum\":\"%016llx\",\"after_timed_window\":true}\n",failures?"false":"true",chains,components,nans,twips,failures,(unsigned long long)checksum);std::fclose(f);return !failures;
}
