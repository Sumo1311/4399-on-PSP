#pragma once
#include "recorded_renderer.hpp"

// Immutable first-ending render recipe from the retained source/input route.
// Used only for texture-cache preparation. It is never submitted for drawing.
// Discovery SHA256: 1219158b2183e585c6ec3f656b9251dae86be69b603d33c7116f32b4095315bf
namespace dnm {
inline psp60::Frame first_ending_resources(){
    psp60::Frame f;f.x=59;f.y=0;f.w=362;f.h=272;f.x0=0;f.x1=12800;f.y0=0;f.y1=9600;
    f.commands.reserve(8);
    {psp60::Command c;c.op=psp60::Op::Shape;c.a=926;c.b=0;
     c.m.m_[0][0]=float(42);
     c.m.m_[0][1]=float(0);
     c.m.m_[0][2]=float(-100);
     c.m.m_[1][0]=float(0);
     c.m.m_[1][1]=float(42);
     c.m.m_[1][2]=float(-100);
     c.previous=c.m;f.commands.push_back(c);}
    {psp60::Command c;c.op=psp60::Op::Shape;c.a=927;c.b=0;
     c.m.m_[0][0]=float(42);
     c.m.m_[0][1]=float(0);
     c.m.m_[0][2]=float(-100);
     c.m.m_[1][0]=float(0);
     c.m.m_[1][1]=float(42);
     c.m.m_[1][2]=float(-100);
     c.previous=c.m;f.commands.push_back(c);}
    {psp60::Command c;c.op=psp60::Op::Shape;c.a=1427;c.b=0;
     c.m.m_[0][0]=float(15.75);
     c.m.m_[0][1]=float(0);
     c.m.m_[0][2]=float(5556);
     c.m.m_[1][0]=float(0);
     c.m.m_[1][1]=float(15.75);
     c.m.m_[1][2]=float(4709);
     c.previous=c.m;f.commands.push_back(c);}
    {psp60::Command c;c.op=psp60::Op::Shape;c.a=1494;c.b=0;
     c.m.m_[0][0]=float(30.0085);
     c.m.m_[0][1]=float(0);
     c.m.m_[0][2]=float(9726.51);
     c.m.m_[1][0]=float(0);
     c.m.m_[1][1]=float(33.0746);
     c.m.m_[1][2]=float(4830.05);
     c.previous=c.m;f.commands.push_back(c);}
    {psp60::Command c;c.op=psp60::Op::Shape;c.a=1494;c.b=0;
     c.m.m_[0][0]=float(30.0085);
     c.m.m_[0][1]=float(0);
     c.m.m_[0][2]=float(2986.64);
     c.m.m_[1][0]=float(0);
     c.m.m_[1][1]=float(33.0746);
     c.m.m_[1][2]=float(4830.05);
     c.previous=c.m;f.commands.push_back(c);}
    {psp60::Command c;c.op=psp60::Op::Shape;c.a=606;c.b=0;
     c.m.m_[0][0]=float(9.81779);
     c.m.m_[0][1]=float(0);
     c.m.m_[0][2]=float(-100);
     c.m.m_[1][0]=float(0);
     c.m.m_[1][1]=float(10.1254);
     c.m.m_[1][2]=float(-100);
     c.previous=c.m;f.commands.push_back(c);}
    {psp60::Command c;c.op=psp60::Op::Shape;c.a=1868;c.b=0;
     c.m.m_[0][0]=float(21);
     c.m.m_[0][1]=float(0);
     c.m.m_[0][2]=float(4908);
     c.m.m_[1][0]=float(0);
     c.m.m_[1][1]=float(21);
     c.m.m_[1][2]=float(1061);
     c.previous=c.m;f.commands.push_back(c);}
    {psp60::Command c;c.op=psp60::Op::Shape;c.a=1870;c.b=0;
     c.m.m_[0][0]=float(21);
     c.m.m_[0][1]=float(0);
     c.m.m_[0][2]=float(6340);
     c.m.m_[1][0]=float(0);
     c.m.m_[1][1]=float(21);
     c.m.m_[1][2]=float(6118);
     c.previous=c.m;f.commands.push_back(c);}
    return f;
}
}
