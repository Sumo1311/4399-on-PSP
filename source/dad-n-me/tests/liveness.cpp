#include "gameswf/gameswf_player.h"
#include "gameswf/gameswf_object.h"
#include <cstdio>
#include <cstdlib>
using namespace gameswf;
static unsigned checks=0;
static void check(bool ok){++checks;if(!ok){std::fprintf(stderr,"liveness mismatch at %u\n",checks);std::exit(1);}}
static void agree(player* p,as_object* o){bool expected=false;p->m_heap.get(o,&expected);check(p->is_garbage(o)==expected);}
int main(){
 gc_ptr<player> a=new player,b=new player;
 gc_ptr<as_object> object=new as_object(a.get_ptr());
 // Reading an absent object must not suppress its later registration.
 check(!a->is_garbage(object));check(a->m_heap.find(object)==a->m_heap.end());
 as_object* raw=new as_object(a.get_ptr());weak_ptr<as_object> raw_lifetime=raw;
 check(!a->is_garbage(raw));check(raw_lifetime.get_ptr()==nullptr);
 a->set_alive(object);check(a->m_heap.find(object)!=a->m_heap.end());agree(a,object);
 for(unsigned n=0;n<400;++n){
  a->set_as_garbage();agree(a,object);check(a->is_garbage(object));
  a->set_alive(object);agree(a,object);check(!a->is_garbage(object));
  a->set_alive(object);agree(a,object);
 }
 // One object can be present in two heaps with different mark states.
 b->set_alive(object);a->set_as_garbage();
 for(unsigned n=0;n<100;++n){agree(a,object);agree(b,object);check(a->is_garbage(object));check(!b->is_garbage(object));}
 a->set_alive(object);b->set_as_garbage();agree(a,object);agree(b,object);check(!a->is_garbage(object));check(b->is_garbage(object));
 // Sweep removal followed by resurrection must invalidate cached membership.
 a->set_as_garbage();a->clear_garbage();check(a->m_heap.find(object)==a->m_heap.end());check(!a->is_garbage(object));
 a->set_alive(object);check(a->m_heap.find(object)!=a->m_heap.end());agree(a,object);agree(b,object);
 gc_ptr<as_object> peer=new as_object(a.get_ptr());a->set_alive(peer);
 object->set_member("peer",peer.get_ptr());peer->set_member("peer",object.get_ptr());
 a->set_as_garbage();object->this_alive();check(!a->is_garbage(object));check(!a->is_garbage(peer));agree(a,object);agree(a,peer);
 // A new script-member copy must not inherit another object's heap membership.
 gc_ptr<as_object> copied=new as_object(a.get_ptr());object->copy_to(copied);check(a->m_heap.find(copied)==a->m_heap.end());check(!a->is_garbage(copied));a->set_alive(copied);agree(a,copied);
 a->set_alive(nullptr);check(!a->is_garbage(nullptr));
 a->clear_heap();check(a->m_heap.size()==0);check(!a->is_garbage(object));a->set_alive(object);check(a->m_heap.find(object)!=a->m_heap.end());agree(a,object);
 std::printf("{\"checks\":%u,\"pass\":true}\n",checks);
}
