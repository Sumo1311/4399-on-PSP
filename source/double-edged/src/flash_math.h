#ifndef DOUBLE_EDGED_FLASH_MATH_H
#define DOUBLE_EDGED_FLASH_MATH_H
#include <math.h>
#include <stdint.h>
/* Flash display transforms round each float operation, then add integral
 * translations in twips. Keep the script and drawing paths on this formula. */
static inline int32_t de_round_twips(float value) {
    if(!isfinite(value))return 0;
    if(value>=2147483648.f||value< -2147483648.f)return INT32_MIN;
    int32_t integral=(int32_t)value;float fraction=value-(float)integral;
    if(fraction>.5f||(fraction==.5f&&(integral&1)))++integral;
    if(fraction<-.5f||(fraction==-.5f&&(integral&1)))--integral;
    return integral;
}
static inline uint32_t de_wrap_u32(double value) {
    if(!isfinite(value))return 0;
    if(value>=0&&value<4294967296.)return (uint32_t)value;
    if(value>=-2147483648.&&value<0)return (uint32_t)(int32_t)value;
    double wrapped=fmod(trunc(value),4294967296.);
    if(wrapped<0)wrapped+=4294967296.;
    return (uint32_t)wrapped;
}
static inline float de_matrix_term(double a,double b,double c,double d) {
    float first=(float)a*(float)b,second=(float)c*(float)d;
    return (float)(first+second);
}
static inline double de_translate_twips(float value,double translation) {
    double units=translation*20;uint32_t parent;
    if(units>=-2147483648.&&units<2147483647.){
        int32_t integral=(int32_t)units;double fraction=units-integral;
        if(fraction>=.5)++integral;
        if(fraction<-.5)--integral;
        parent=(uint32_t)integral;
    }else parent=de_wrap_u32(floor(units+.5));
    uint32_t result=(uint32_t)de_round_twips(value)+parent;
    return (double)(int32_t)result/20;
}
static inline void de_matrix_values(const double a[6],const double b[6],double result[6]) {
    float x=(float)(b[4]*20),y=(float)(b[5]*20);
    result[0]=de_matrix_term(a[0],b[0],a[2],b[1]);result[1]=de_matrix_term(a[1],b[0],a[3],b[1]);
    result[2]=de_matrix_term(a[0],b[2],a[2],b[3]);result[3]=de_matrix_term(a[1],b[2],a[3],b[3]);
    result[4]=de_translate_twips(de_matrix_term(a[0],x,a[2],y),a[4]);result[5]=de_translate_twips(de_matrix_term(a[1],x,a[3],y),a[5]);
}
static inline void de_bounds_values(const double bounds[4],const double matrix[6],double result[4]) {
    if(matrix[1]==0&&matrix[2]==0){
        for(unsigned axis=0;axis<2;axis++){
            unsigned i=axis*2;double first=de_translate_twips((float)matrix[axis?3:0]*(float)(bounds[i]*20),matrix[4+axis]);
            double second=de_translate_twips((float)matrix[axis?3:0]*(float)(bounds[i+1]*20),matrix[4+axis]);
            result[i]=first<second?first:second;result[i+1]=first>second?first:second;
        }
        return;
    }
    for(unsigned i=0;i<4;i++){
        float x=(float)(bounds[i&1]*20),y=(float)(bounds[2+(i>>1)]*20);
        double px=de_translate_twips(de_matrix_term(matrix[0],x,matrix[2],y),matrix[4]);
        double py=de_translate_twips(de_matrix_term(matrix[1],x,matrix[3],y),matrix[5]);
        if(!i){result[0]=result[1]=px;result[2]=result[3]=py;}
        else{if(px<result[0])result[0]=px;if(px>result[1])result[1]=px;if(py<result[2])result[2]=py;if(py>result[3])result[3]=py;}
    }
}
#endif
