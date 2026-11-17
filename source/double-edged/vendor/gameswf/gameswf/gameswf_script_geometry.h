#pragma once
#include "gameswf/gameswf_types.h"
namespace gameswf {
struct character;
// AVM1 geometry uses integer twips, including matrix translations. Combine
// transforms before enclosing a leaf so nested rotations do not inflate bounds.
matrix script_product(const matrix& parent,const matrix& local);
matrix script_world(character*);
matrix script_inverse(const matrix&);
point script_point(const matrix&,const point&);
bool script_valid(const rect&);
void script_empty(rect*);
void script_transform(rect*,const matrix&);
// Rectangle is already in pixels; the query is in the root movie's pixels.
bool script_point_hit(const rect&,const matrix& root_world,double x,double y);
}
