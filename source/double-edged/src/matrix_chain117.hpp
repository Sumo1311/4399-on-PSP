#pragma once
#include "flash_math.h"
#include <cstring>
/* Keep the four already-rounded coefficients as float between folds. Keep
 * rounded translations as integer twips until their final number is needed.
 * Large translations retain the original double divide/multiply conversion. */
static inline int32_t de_chain_translate117(float value,double translation,unsigned* zero_count){
 uint64_t raw;std::memcpy(&raw,&translation,sizeof(raw));if((raw<<1)==0){if(zero_count)++*zero_count;return de_round_twips(value);}
 double units=translation*20;uint32_t parent;
 if(units>=-2147483648.&&units<2147483647.){
  int32_t integral=(int32_t)units;double fraction=units-integral;
  if(fraction>=.5)++integral;if(fraction<-.5)--integral;parent=(uint32_t)integral;
 }else parent=de_wrap_u32(floor(units+.5));
 return (int32_t)((uint32_t)de_round_twips(value)+parent);
}
static inline float de_chain_input117(int32_t twips){
 // Every integer in this range is exactly representable in binary32. The
 // original binary64 (twips/20)*20 lies within that float's rounding interval.
 if(twips>=-16777216&&twips<=16777216)return (float)twips;
 return (float)(((double)twips/20)*20);
}
static inline void de_matrix_chain117(const double(*matrices)[6],unsigned count,double result[6],unsigned* zero_count=nullptr){
 if(count<2){for(unsigned j=0;j<6;j++)result[j]=matrices[0][j];return;}
 float b0=(float)matrices[0][0],b1=(float)matrices[0][1],b2=(float)matrices[0][2],b3=(float)matrices[0][3];
 float x=(float)(matrices[0][4]*20),y=(float)(matrices[0][5]*20);int32_t tx=0,ty=0;
 for(unsigned i=1;i<count;i++){
  const double* a=matrices[i];float a0=(float)a[0],a1=(float)a[1],a2=(float)a[2],a3=(float)a[3];
  float c0=(float)((float)(a0*b0)+(float)(a2*b1)),c1=(float)((float)(a1*b0)+(float)(a3*b1));
  float c2=(float)((float)(a0*b2)+(float)(a2*b3)),c3=(float)((float)(a1*b2)+(float)(a3*b3));
  tx=de_chain_translate117((float)((float)(a0*x)+(float)(a2*y)),a[4],zero_count);
  ty=de_chain_translate117((float)((float)(a1*x)+(float)(a3*y)),a[5],zero_count);
  b0=c0;b1=c1;b2=c2;b3=c3;
  if(i+1<count){x=de_chain_input117(tx);y=de_chain_input117(ty);}
 }
 result[0]=b0;result[1]=b1;result[2]=b2;result[3]=b3;result[4]=(double)tx/20;result[5]=(double)ty/20;
}
