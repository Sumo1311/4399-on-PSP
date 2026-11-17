#include <cstdint>
#include <cstdlib>
#include <cstdio>
// gameswf_function.cpp	-- Thatcher Ulrich <tu@tulrich.com> 2003

// This source code has been donated to the Public Domain.  Do
// whatever you want with it.

// ActionScript function.

#include "gameswf/gameswf_function.h"
#include "gameswf/gameswf_log.h"
#include "gameswf/gameswf_character.h"
#include "gameswf/gameswf_sprite.h"
#include "gameswf/gameswf_as_classes/as_array.h"

#ifdef GLORTON_FUNCTION_PROFILE
#include <chrono>
#include <map>
#include <string>
#include <cstdlib>
namespace {
using ProfileClock=std::chrono::steady_clock;
struct Record { unsigned long long calls=0,inclusive=0,exclusive=0; };
struct Records {
 std::map<std::string,Record> data;
 ~Records(){const char* path=std::getenv("GLORTON_PROFILE_PATH");if(!path)return;
 FILE* f=std::fopen(path,"w");if(!f)return;
 std::fprintf(f,"function,calls,inclusive_ns,exclusive_ns\n");
 for(const auto& p:data)std::fprintf(f,"%s,%llu,%llu,%llu\n",p.first.c_str(),p.second.calls,p.second.inclusive,p.second.exclusive);
 std::fclose(f);}
};
struct ProfileScope {
 static ProfileScope* current;
 ProfileScope* parent; Record* record; ProfileClock::time_point began; unsigned long long children=0;
 ProfileScope(const gameswf::as_s_function* f){static Records records;
 std::string key=f->m_profile_name.c_str();key+=":"+std::to_string(f->m_start_pc)+":"+std::to_string(f->m_length);
 record=&records.data[key];parent=current;current=this;began=ProfileClock::now();}
 ~ProfileScope(){auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(ProfileClock::now()-began).count();
 ++record->calls;record->inclusive+=elapsed;record->exclusive+=elapsed-children;if(parent)parent->children+=elapsed;current=parent;}
};
ProfileScope* ProfileScope::current=nullptr;
}
#endif
namespace gameswf
{
    static script_accelerator source_accelerator=nullptr;
    void register_script_accelerator(script_accelerator hook){source_accelerator=hook;}

    // A method inherited by a further subclass starts super lookup above the
    // prototype that owns that method, rather than above the receiver's class.
    // The engine's per-instance prototype chain retains the original methods,
    // so the owner can be found without changing ActionScript-visible objects.
    static as_object* method_super(const as_s_function* function,as_object* receiver) {
        as_object* fallback=receiver?receiver->get_proto():nullptr;
        for(as_object* owner=receiver;owner;owner=owner->get_proto()) {
            for(auto it=owner->m_members.begin();it!=owner->m_members.end();++it)
                if(it->second.is_function() && it->second.to_function()==function)return owner->get_proto();
        }
        return fallback;
    }

#if GAMESWF_SHARED_AVM1_STACK
    // Native bridge arguments are held outside the AVM1 operand stack during
    // execution. Restore the old bridge layout above any bytecode leftovers so
    // existing callers can remove their arguments without discarding leftovers.
    struct operand_arguments {
        as_value receiver,empty,result;
        as_value* destination;int destination_slot=-1;
        array<as_value> values,saved;
        fn_call call;
        as_environment* env;
        bool restore=false;
        operand_arguments(const fn_call& incoming):receiver(incoming.this_value),destination(incoming.result),call(&result,receiver,incoming.env,incoming.nargs,incoming.first_arg_bottom_index),env(incoming.env) {
            if(env && env->size()) {
                auto begin=reinterpret_cast<uintptr_t>(&env->bottom(0)),end=begin+env->size()*sizeof(as_value),pointer=reinterpret_cast<uintptr_t>(destination);
                if(pointer>=begin && pointer<end && (pointer-begin)%sizeof(as_value)==0)destination_slot=(pointer-begin)/sizeof(as_value);
            }
            if(incoming.argument_values){call.argument_values=incoming.argument_values;return;}
            if(!env || incoming.nargs<0){call.argument_values=&empty;return;}
            values.resize(incoming.nargs);
            for(int i=0;i<incoming.nargs;++i)values[i]=incoming.arg(i);
            int prefix=incoming.first_arg_bottom_index-incoming.nargs+1;
            if(prefix<0)prefix=0;if(prefix>env->size())prefix=env->size();
            saved.resize(env->size()-prefix);for(int i=0;i<saved.size();++i)saved[i].copy_raw(env->bottom(prefix+i));
            env->resize(prefix);call.argument_values=values.size()?&values[0]:&empty;restore=true;
        }
        ~operand_arguments(){
            if(restore)for(int i=0;i<saved.size();++i){env->push(as_value());env->top(0).copy_raw(saved[i]);}
            if(destination_slot>=0)env->bottom(destination_slot)=result;else if(destination)*destination=result;
        }
    };
#endif

	// Invokes the function represented by a Function object.
	// public call(thisObject:Object, [parameter1:Object]) : Object
	void	as_s_function_call(const fn_call& fn)
	{
		assert(fn.this_ptr);
		if (fn.nargs > 0)
		{
			as_object* properties = fn.this_ptr;
			if (properties->m_this_ptr != NULL)
			{
				as_object* func = properties->m_this_ptr.get_ptr();
				if (func)
				{
					as_environment env(fn.get_player());
					int nargs = 0;
					if (fn.nargs > 1)
					{
						nargs = 1;
						env.push(fn.arg(1));
					}

					*fn.result = call_method(func, &env, fn.arg(0).to_object(),
						nargs, env.get_top_index());
				}
			}
		}
	}

	as_s_function::as_s_function(player* player,
		const action_buffer* ab, int start, const array<with_stack_entry>& with_stack) :
		as_function(player),
		m_with_stack(with_stack),
		m_start_pc(start),
		m_length(0),
		m_is_function2(false),
		m_local_register_count(0),
		m_function2_flags(0)
	{
		// Flash stores a body(functions) of a class in _global,
		// action_buffer is in movie_def, therefore when we load another _root
		// action_buffer will be deleted and _global.MyClass will keep
		// the pointer to the removed object(action_buffer).
		// Therefore we have updated action_buffer* ==> action_buffer in as_s_function
		assert(ab);
		m_action_buffer = *ab;

		m_this_ptr = this;

		// any function MUST have prototype
		builtin_member("prototype", new as_object(player));

		builtin_member("call", as_s_function_call);
	}

	as_s_function::~as_s_function()
	{
	}

	void	as_s_function::operator()(const fn_call& incoming)
    // Dispatch.
    {
#if GAMESWF_SHARED_AVM1_STACK
        operand_arguments arguments(incoming);const fn_call& fn=arguments.call;
#else
        const fn_call& fn=incoming;
#endif
#ifdef GLORTON_FUNCTION_PROFILE
        ProfileScope profile(this);
#endif
        if(source_accelerator && source_accelerator(*this,fn))return;

		assert(fn.env);

		// Keep target alive during execution!
		gc_ptr<as_object> target(m_target.get_ptr());

		// try to use caller environment
		// if the caller object has own environment then we use its environment
		as_environment* env = fn.env;
		if (fn.this_ptr)
		{
			if (fn.this_ptr->get_environment())
			{
				env = fn.this_ptr->get_environment();
			}
		}

		// set 'this'
		as_object* this_ptr = env->get_target();
		if (fn.this_ptr)
		{
			this_ptr = fn.this_ptr;
			if (this_ptr->m_this_ptr != NULL)
			{
				this_ptr = this_ptr->m_this_ptr.get_ptr();
			}
		}

		// Function has been declared in moviclip ==> we should use its environment
		// At the same time 'this_ptr' may refers to another object
		// see testcase in .h file
		if (m_target != NULL)
		{
			character* ch = cast_to<character>(m_target.get_ptr());
			if (ch)
			{
				// The live root has depth zero, so the display-list depth test
				// alone incorrectly loses lexical scope for root-defined callbacks.
				if (ch->is_alive() || (ch->get_root() && ch==ch->get_root()->get_root_movie()))
				{
					env = m_target->get_environment();
				}
			}
		}

		// Set up local stack frame, for parameters and locals.
		int	local_stack_top = env->get_local_frame_top();
		env->add_frame_barrier();

        as_object* super_proxy=nullptr;
        if(!m_is_function2 || (m_function2_flags&0x10) || !(m_function2_flags&0x20))
            super_proxy=method_super(this,fn.this_ptr);

		if (m_is_function2 == false)
		{
			// Conventional function.

			// Push the arguments onto the local frame.
			int	args_to_pass = imin(fn.nargs, m_args.size());
			for (int i = 0; i < args_to_pass; i++)
			{
				assert(m_args[i].m_register == 0);
				env->add_local(m_args[i].m_name, fn.arg(i));
			}

			env->set_local("this", this_ptr);

			// Put 'super' in a local var.
			if (fn.this_ptr)
			{
				env->add_local("super", super_proxy);
			}
		}
		else
		{
			// function2: most args go in registers; any others get pushed.
			
			// Create local registers.
			env->add_local_registers(m_local_register_count);

			// Handle the explicit args.
			int	args_to_pass = imin(fn.nargs, m_args.size());
			for (int i = 0; i < args_to_pass; i++)
			{
				if (m_args[i].m_register == 0)
				{
					// Conventional arg passing: create a local var.
					env->add_local(m_args[i].m_name, fn.arg(i));
				}
				else
				{
					// Pass argument into a register.
					int	reg = m_args[i].m_register;
					env->set_register(reg, fn.arg(i));
				}
			}

			// Handle the implicit args.
			int	current_reg = 1;

			if (m_function2_flags & 0x01)
			{
				// preload 'this' into a register.
				IF_VERBOSE_ACTION(log_msg("-------------- preload this=%p to register %d\n",
					this_ptr, current_reg));
				env->set_register(current_reg, this_ptr);
				current_reg++;

			}

			if (m_function2_flags & 0x02)
			{
				// Don't put 'this' into a local var.
			}
			else
			{
				// Put 'this' in a local var.
				env->add_local("this", as_value(this_ptr));
			}

			// Init arguments array, if it's going to be needed.
			gc_ptr<as_array>	arg_array;
			if ((m_function2_flags & 0x04) || ! (m_function2_flags & 0x08))
			{
				arg_array = new as_array(env->get_player());

				as_value	index_number;
				for (int i = 0; i < fn.nargs; i++)
				{
					index_number.set_int(i);
					arg_array->set_member(index_number.to_string(), fn.arg(i));
				}
			}

			if (m_function2_flags & 0x04)
			{
				// preload 'arguments' into a register.
				env->set_register(current_reg, arg_array.get_ptr());
				current_reg++;
			}

			if (m_function2_flags & 0x08)
			{
				// Don't put 'arguments' in a local var.
			}
			else
			{
				// Put 'arguments' in a local var.
 				env->add_local("arguments", as_value(arg_array.get_ptr()));
			}

			if (m_function2_flags & 0x10)
			{
				// Put 'super' in a register.
				IF_VERBOSE_ACTION(log_msg("-------------- preload super=%p to register %d\n",
					super_proxy, current_reg));
				env->set_register(current_reg, super_proxy);
				current_reg++;
			}

			if (m_function2_flags & 0x20)
			{
				// Don't put 'super' in a local var.
			}
			else
			{
				// Put 'super' in a local var.
				env->add_local("super", super_proxy);
			}

			if (m_function2_flags & 0x40)
			{
				// Put '_root' in a register.
				env->set_register(current_reg, env->get_root()->get_root_movie());
				current_reg++;
			}

			if (m_function2_flags & 0x80)
			{
				// Put '_parent' in a register.
				array<with_stack_entry>	dummy;
				as_value	parent = env->get_variable("_parent", dummy);
				IF_VERBOSE_ACTION(log_msg("-------------- preload _parent=%p to register %d\n", parent.to_object(), current_reg));
				env->set_register(current_reg, parent);
				current_reg++;
			}

			if (m_function2_flags & 0x100)
			{
				// Put '_global' in a register.
				IF_VERBOSE_ACTION(log_msg("-------------- preload _global=%p to register %d\n", 
					get_global(), current_reg));
				env->set_register(current_reg, get_global());
				current_reg++;
			}
		}

		// keep stack size
		int stack_size = env->get_stack_size();

		// Execute the actions.
		m_action_buffer.execute(env, m_start_pc, m_length, fn.result, m_with_stack, m_is_function2);

#if !GAMESWF_SHARED_AVM1_STACK
		// restore stack size
		// it should not be but it happens
		if (stack_size != env->get_stack_size())
		{
#ifndef PSP
            if(std::getenv("GLORTON_STACK_DIAGNOSTIC"))std::fprintf(stderr,"NATIVE_STACK pc=%d length=%d entry=%d exit=%d delta=%d\n",m_start_pc,m_length,stack_size,env->get_stack_size(),env->get_stack_size()-stack_size);
#endif
//			log_error("s_function: on entry stack size (%d) != on exit stack size (%d)\n", 
//				stack_size, env->m_stack.size());
			env->set_stack_size(stack_size);
		}

#endif

		// Clean up stack frame.
		env->set_local_frame_top(local_stack_top);

		if (m_is_function2)
		{
			// Clean up the local registers.
			env->drop_local_registers(m_local_register_count);
		}
				
	}

	as_c_function::as_c_function(player* player, as_c_function_ptr func) :
		as_function(player),
		m_func(func)
	{
		// any function MUST have prototype
		builtin_member("prototype", new as_object(player));
	}

	void	as_c_function::operator()(const fn_call& incoming)
	{
#if GAMESWF_SHARED_AVM1_STACK
        operand_arguments arguments(incoming);const fn_call& fn=arguments.call;
#else
        const fn_call& fn=incoming;
#endif
		if (m_func)
		{
			(*m_func)(fn);
		}
	}
}
