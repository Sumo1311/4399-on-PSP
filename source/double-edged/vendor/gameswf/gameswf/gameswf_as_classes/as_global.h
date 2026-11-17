// as_global.h	-- Thatcher Ulrich <tu@tulrich.com> 2003

// This source code has been donated to the Public Domain.  Do
// whatever you want with it.

// Action Script global functions implementation


#ifndef GAMESWF_AS_GLOBAL_H
#define GAMESWF_AS_GLOBAL_H

namespace gameswf
{
	void	as_global_trace(const fn_call& fn);
	void	as_global_get_version(const fn_call& fn);
	void	as_global_assetpropflags(const fn_call& fn);
	void  as_global_setinterval(const fn_call& fn);
	void  as_global_clearinterval(const fn_call& fn);
	void  as_global_settimeout(const fn_call& fn);
	void  as_global_cleartimeout(const fn_call& fn);
	void	as_global_update_after_event(const fn_call& fn);

	// for setInterval and setTimeout
	struct as_timer : public as_object
	{
		gc_ptr<as_object> m_this_ptr;
		gc_ptr<as_function> m_func;
        bool m_is_method=false;
        tu_string m_method_name;
		Uint32 m_timer_id=0;
		Uint64 m_next_due_us=0;
		Uint64 m_period_us=0;
		float m_interval;	// sec
		float m_time_remainder;
		array<as_value> m_arg;
		bool m_do_once;

		// Unique id of a gameswf resource
		enum { m_class_id = AS_TIMER };
		virtual bool is(int class_id) const
		{
			if (m_class_id == class_id) return true;
			else return as_object::is(class_id);
		}

		as_timer(player* player) :
			as_object(player),
			m_interval(0.0f),
			m_time_remainder(0.0f),
			m_do_once(false)
		{
		}

		virtual void advance(float delta_time);
		virtual void this_alive() {
			as_object::this_alive();
			if(m_func)m_func->this_alive();
			if(m_this_ptr)m_this_ptr->this_alive();
		}
	};


}	// end namespace gameswf


#endif // GAMESWF_AS_GLOBAL_H


// Local Variables:
// mode: C++
// c-basic-offset: 8 
// tab-width: 8
// indent-tabs-mode: t
// End:
