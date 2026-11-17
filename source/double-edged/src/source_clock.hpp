#pragma once
#include <cstdint>

// Original SWF:30Hz. Integer rational clock; logic never depends on how many
// display frames were submitted. Reset only across explicit loading/help.
class SourceClock {
    std::uint64_t next_=0;
    unsigned fraction_=0;
public:
    void reset(std::uint64_t now){next_=now;fraction_=0;}
    std::uint64_t next()const{return next_;}
    std::uint64_t wait(std::uint64_t now)const{return now<next_?next_-now:0;}
    std::uint64_t lag(std::uint64_t now)const{return now>next_?now-next_:0;}
    void advance(){next_+=33333;if(++fraction_==3){++next_;fraction_=0;}}
};
