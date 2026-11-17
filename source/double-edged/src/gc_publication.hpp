#pragma once
#include <utility>
namespace depublication {
// Both callbacks finish before the collector starts. The producer must keep
// the published packet immutable; only its private timing record changes.
template<class Prepare,class Publish>
inline bool before_collection(unsigned reason,bool captured,bool loading,
                              bool bounded_final,Prepare&& prepare,Publish&& publish){
    if((reason!=1&&reason!=2&&reason!=4)||!captured||loading||bounded_final)return false;
    std::forward<Prepare>(prepare)();std::forward<Publish>(publish)();return true;
}
}
