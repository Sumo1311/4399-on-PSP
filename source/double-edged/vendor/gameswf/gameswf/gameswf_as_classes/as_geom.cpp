// as_geom.cpp	-- Julien Hamaide <julien.hamaide@gmail.com>	2008

// This source code has been donated to the Public Domain.  Do
// whatever you want with it.

#include "gameswf/gameswf_as_classes/as_geom.h"
#include "gameswf/gameswf_as_classes/as_point.h"
#include "gameswf/gameswf_as_classes/as_matrix.h"
#include "gameswf/gameswf_as_classes/as_transform.h"
#include "gameswf/gameswf_as_classes/as_color_transform.h"

namespace gameswf
{
	static double rectangle_number(as_object* object,const char* name) {
		as_value value;return object && object->get_member(name,&value)?value.to_number():0;
	}
	static void rectangle_contains_point(const fn_call& fn) {
		as_object* p=fn.nargs?fn.arg(0).to_object():NULL;
		double x=rectangle_number(fn.this_ptr,"x"),y=rectangle_number(fn.this_ptr,"y"),w=rectangle_number(fn.this_ptr,"width"),h=rectangle_number(fn.this_ptr,"height");
		double px=rectangle_number(p,"x"),py=rectangle_number(p,"y");
		fn.result->set_bool(p && w>0 && h>0 && px>=x && py>=y && px<x+w && py<y+h);
	}
	struct as_rectangle : as_object {
		as_rectangle(player* owner,double x,double y,double w,double h):as_object(owner) {
			as_object::set_member("x",as_value(x));as_object::set_member("y",as_value(y));
			as_object::set_member("width",as_value(w));as_object::set_member("height",as_value(h));
			builtin_member("containsPoint",rectangle_contains_point);
		}
		bool get_member(const tu_stringi& name,as_value* result) override {
			if(name=="left") {result->set_double(rectangle_number(this,"x"));return true;}
			if(name=="top") {result->set_double(rectangle_number(this,"y"));return true;}
			if(name=="right") {result->set_double(rectangle_number(this,"x")+rectangle_number(this,"width"));return true;}
			if(name=="bottom") {result->set_double(rectangle_number(this,"y")+rectangle_number(this,"height"));return true;}
			return as_object::get_member(name,result);
		}
		bool set_member(const tu_stringi& name,const as_value& value) override {
			if(name=="left") {
				double right=rectangle_number(this,"x")+rectangle_number(this,"width");
				as_object::set_member("width",as_value(right-value.to_number()));return as_object::set_member("x",value);
			}
			if(name=="top") {
				double bottom=rectangle_number(this,"y")+rectangle_number(this,"height");
				as_object::set_member("height",as_value(bottom-value.to_number()));return as_object::set_member("y",value);
			}
			if(name=="right")return as_object::set_member("width",as_value(value.to_number()-rectangle_number(this,"x")));
			if(name=="bottom")return as_object::set_member("height",as_value(value.to_number()-rectangle_number(this,"y")));
			return as_object::set_member(name,value);
		}
	};
	static void as_global_rectangle_ctor(const fn_call& fn) {
		double values[4]={0,0,0,0};for(int i=0;i<fn.nargs && i<4;++i)values[i]=fn.arg(i).to_number();
		fn.result->set_as_object(new as_rectangle(fn.get_player(),values[0],values[1],values[2],values[3]));
	}
	//
	// geom object
	//

	as_object* geom_init(player* player)
	{
		// Create built-in geom object.
		as_object*	geom_obj = new as_object(player);

		// constant
		geom_obj->builtin_member("Point", as_global_point_ctor);
		geom_obj->builtin_member("Rectangle", as_global_rectangle_ctor);
		geom_obj->builtin_member("Matrix", as_global_matrix_ctor);
		geom_obj->builtin_member("Transform", as_global_transform_ctor);
		geom_obj->builtin_member("ColorTransform", as_global_color_transform_ctor);

		return geom_obj;
	}

}
