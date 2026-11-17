#include "gameswf/gameswf_script_geometry.h"
#include "gameswf/gameswf_character.h"
#include "gameswf/gameswf_script_rounding.h"
#include <cmath>
#include <cfloat>
namespace gameswf {
static float twip(float n){return script_round_twip(n);}
matrix script_product(const matrix& p,const matrix& l) {
    matrix m;
    m.m_[0][0]=p.m_[0][0]*l.m_[0][0]+p.m_[0][1]*l.m_[1][0];
    m.m_[0][1]=p.m_[0][0]*l.m_[0][1]+p.m_[0][1]*l.m_[1][1];
    m.m_[1][0]=p.m_[1][0]*l.m_[0][0]+p.m_[1][1]*l.m_[1][0];
    m.m_[1][1]=p.m_[1][0]*l.m_[0][1]+p.m_[1][1]*l.m_[1][1];
    m.m_[0][2]=twip(p.m_[0][0]*l.m_[0][2]+p.m_[0][1]*l.m_[1][2])+p.m_[0][2];
    m.m_[1][2]=twip(p.m_[1][0]*l.m_[0][2]+p.m_[1][1]*l.m_[1][2])+p.m_[1][2];return m;
}
matrix script_world(character* ch){return ch?script_product(script_world(ch->get_parent()),ch->get_matrix()):matrix();}
matrix script_inverse(const matrix& m) {
    float a=m.m_[0][0],b=m.m_[1][0],c=m.m_[0][1],d=m.m_[1][1],tx=m.m_[0][2],ty=m.m_[1][2],det=a*d-b*c;
    matrix inv;if(std::fabs(det)<=FLT_EPSILON)return inv;
    inv.m_[0][0]=d/det;inv.m_[1][0]=b/-det;inv.m_[0][1]=c/-det;inv.m_[1][1]=a/det;
    inv.m_[0][2]=twip((d*tx-c*ty)/-det);inv.m_[1][2]=twip((b*tx-a*ty)/det);return inv;
}
point script_point(const matrix& m,const point& p){return {twip(m.m_[0][0]*p.m_x+m.m_[0][1]*p.m_y)+m.m_[0][2],twip(m.m_[1][0]*p.m_x+m.m_[1][1]*p.m_y)+m.m_[1][2]};}
bool script_valid(const rect& b){return b.m_x_min<=b.m_x_max && b.m_y_min<=b.m_y_max;}
void script_empty(rect* b){b->m_x_min=b->m_y_min=FLT_MAX;b->m_x_max=b->m_y_max=-FLT_MAX;}
void script_transform(rect* b,const matrix& m) {
    if(!script_valid(*b))return;
    point p[4];for(int i=0;i<4;++i)p[i]=script_point(m,b->get_corner(i));
    b->set_to_point(p[0]);for(int i=1;i<4;++i)b->expand_to_point(p[i]);
}
bool script_point_hit(const rect& bounds,const matrix& root_world,double x,double y) {
    point world=script_point(root_world,point(float(trunc(x*20.0)),float(trunc(y*20.0))));
    x=double(world.m_x)/20.0;y=double(world.m_y)/20.0;
    return bounds.point_test(float(x),float(y));
}
}
