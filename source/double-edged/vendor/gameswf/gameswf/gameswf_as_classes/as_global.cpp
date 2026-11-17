// as_global.h	-- Thatcher Ulrich <tu@tulrich.com> 2003
// This source code has been donated to the Public Domain.  Do
// whatever you want with it.

// Action Script global functions implementation

#include "gameswf/gameswf_action.h"	// for as_object
#include "gameswf/gameswf_log.h"
#include "gameswf/gameswf_root.h"
#include "as_global.h"

namespace gameswf
{

	//
	// Built-in objects
	//

	void	as_global_trace(const fn_call& fn)
	{
		assert(fn.nargs >= 1);

		// Log our argument.
		//
		// @@ what if we get extra args?

		const char* val = "";
		if (fn.arg(0).is_undefined())
		{
			val = "undefined";
		} 
		else
		{
			val = fn.arg(0).to_string();
		}
		log_msg("%s\n", val);
	}

	// setInterval(functionReference:Function, interval:Number, [param1:Object, param2, ..., paramN]) : Number
	// setInterval(objectReference:Object, methodName:String, interval:Number, [param1:Object, param2, ..., paramN]) : Number
	void  as_global_setinterval(const fn_call& fn)
	{
		if (fn.nargs >= 2)
		{
			gc_ptr<as_timer> t = new as_timer(fn.get_player());

			int first_arg_index = 2;
			if (fn.arg(0).is_function())
			{
				t->m_func = fn.arg(0).to_function();
				int delay=fn.arg(1).to_int();
                t->m_period_us=Uint64(delay<10?10:delay)*1000;
                t->m_interval = delay / 1000.0f;
				assert(fn.env);
				t->m_this_ptr = fn.get_root()->get_root_movie();
			}
			else
			if (fn.arg(0).to_object() != NULL)
			{
				as_value func;
				as_object* this_ptr = fn.arg(0).to_object();
				t->m_is_method=true;t->m_method_name=fn.arg(1).to_tu_string();
                this_ptr->get_member(t->m_method_name, &func);

				t->m_func = func.to_function();
				int delay=fn.arg(2).to_int();
                t->m_period_us=Uint64(delay<10?10:delay)*1000;
                t->m_interval = delay / 1000.0f;
				t->m_this_ptr = this_ptr;
				first_arg_index = 3;
			}
			else
			{
				// invalid args
				return;
			}

			// pass args
			for (int i = first_arg_index; i < fn.nargs; i++)
			{
				t->m_arg.push_back(fn.arg(i));
			}

			if(!t->m_func && !t->m_is_method)return;
			fn.result->set_int(fn.get_root()->add_timer(t));
		}
	}

	// setTimeout(functionReference:Function, interval:Number, [param1:Object, param2, ..., paramN]) : Number
	// setTimeout(objectReference:Object, methodName:String, interval:Number, [param1:Object, param2, ..., paramN]) : Number
	void  as_global_settimeout(const fn_call& fn)
	{
		// create interval timer
		as_global_setinterval(fn);
		as_timer* t = cast_to<as_timer>(fn.get_root()->get_timer(fn.result->to_int()));

		if (t)
		{
			t->m_do_once = true;
		}
	}

	void  as_global_clearinterval(const fn_call& fn)
	{
		if (fn.nargs > 0)
		{
			fn.get_root()->remove_timer(fn.arg(0).to_int());
		}
	}

	void	as_global_update_after_event(const fn_call& fn)
	{
		// isn't required for gameswf
	}
	
	void	as_global_assetpropflags(const fn_call& fn)
	// Undocumented ASSetPropFlags function
	// Works only for as_object for now
	{
		int version = fn.get_player()->get_root()->get_movie_version();

		// Check the arguments
		assert(fn.nargs == 3 || fn.nargs == 4);
		assert((version == 5) ? (fn.nargs == 3) : true);

		// object
		as_object* obj = fn.arg(0).to_object();
		if (obj == NULL)
		{
			log_error("error: assetpropflags for NULL object\n");
			return;
		}

		// The second argument is a list of child names,
		// may be in the form array(like ["abc", "def", "ggggg"]) or in the form a string(like "abc, def, ggggg")
		// the NULL second parameter means that assetpropflags is applied to all children
		as_object* props = fn.arg(1).to_object();

		int as_prop_flags_mask = 7; // DONT_ENUM | DONT_DELETE | READ_ONLY;

		// a number which represents three bitwise flags which
		// are used to determine whether the list of child names should be hidden,
		// un-hidden, protected from over-write, un-protected from over-write,
		// protected from deletion and un-protected from deletion
		int true_flags = fn.arg(2).to_int() & as_prop_flags_mask;

		// Is another integer bitmask that works like true_flags,
		// except it sets the attributes to false. The
		// false_flags bitmask is applied before true_flags is applied

		// ASSetPropFlags was exposed in Flash 5, however the fourth argument 'false_flags'
		// was not required as it always defaulted to the value '~0'. 
		int false_flags = (fn.nargs == 3 ? 
				 (version == 5 ? ~0 : 0) : fn.arg(3).to_int()) & as_prop_flags_mask;

		// Evan: it seems that if true_flags == 0 and false_flags == 0, this function
		// acts as if the parameters where (object, null, 0x1, 0) ...
		if (false_flags == 0 && true_flags == 0)
		{
			props = NULL;
			false_flags = 0;
			true_flags = 0x1;
		}

		if (props == NULL)
		{
			// Takes all members of the object and sets its property flags
			for (as_member_map::const_iterator it = obj->m_members.begin(); 
				it != obj->m_members.end(); ++it)
			{
				const as_value& val = it->second;
				int flags = val.get_flags();
				flags = flags & (~false_flags);
				flags |= true_flags;
				val.set_flags(flags);
			}
		}
		else
		{
			// Takes all string type prop and sets property flags of obj[prop]
			for (as_member_map::const_iterator it = props->m_members.begin(); 
				it != props->m_members.end(); ++it)
			{
				const as_value& key = it->second;
				if (key.is_string())
				{
					as_member_map::iterator obj_it = obj->m_members.find(key.to_tu_string());
					if (obj_it != obj->m_members.end())
					{
						const as_value& val = obj_it->second;
						int flags = val.get_flags();
						flags = flags & (~false_flags);
						flags |= true_flags;
						val.set_flags(flags);
					}
				}
			}
		}
	}

	// getVersion() : String
	void	as_global_get_version(const fn_call& fn)
	// Returns a string containing Flash Player version and platform information.
	{
		fn.result->set_tu_string(get_gameswf_version());
	}


	void as_timer::advance(float delta_time)
	{
		// Method-form timers resolve the method at each callback.
        as_value method;
        if(m_is_method){if(m_this_ptr)m_this_ptr->get_member(m_method_name,&method);}
        else method=m_func.get_ptr();
        if(!method.to_function())return;

		{
			as_environment env(get_player());
			int n = m_arg.size();
			{
				for (int i = n-1; i >= 0; --i)
				{
					env.push(m_arg[i]);
				}
			}

			// keep alive
			gc_ptr<as_object> obj = m_this_ptr.get_ptr();
			as_value callback(method);

			call_method(callback, &env, obj.get_ptr(), n, env.get_top_index());
#if GAMESWF_SHARED_AVM1_STACK
            env.drop(n);
#endif

		}
	}


};
