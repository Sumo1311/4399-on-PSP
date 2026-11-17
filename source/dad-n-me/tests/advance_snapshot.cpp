#include "gameswf/gameswf_player.h"
#include "gameswf/gameswf_character.h"
#include "gameswf/gameswf_dlist.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace gameswf;
static unsigned checks=0;
static void check(bool ok){++checks;if(!ok){std::fprintf(stderr,"advance snapshot check %u failed\n",checks);std::exit(1);}}
struct Context {player* owner;display_list* list;unsigned mode;bool fired=false;std::vector<int> events;};
struct Actor:character {
 Context* context;int tag;
 Actor(Context* c,int value):character(c->owner,nullptr,value+1),context(c),tag(value){}
 ~Actor(){context->events.push_back(1000+tag);}
 void advance(float delta)override;
};
static void add(Context& c,int id,int depth){c.list->add_display_object(new Actor(&c,id),depth,true,cxform(),matrix(),0,0,0);}
void Actor::advance(float delta){
 check(delta==1.0f/30);context->events.push_back(tag);
 if(tag!=0||context->fired)return;
 context->fired=true;
 switch(context->mode){
  case 1:context->list->clear();add(*context,900,1);break;
  case 2:delete context->list;context->list=nullptr;break;
  case 3:context->list->advance(delta);break;
  case 4:context->list->remove_display_object(context->list->get_character(context->list->size()-1));break;
  default:break;
 }
}
int main(){
 gc_ptr<player> p=new player;
 for(int count:{0,1,2,7,8,9,16,400})for(unsigned mode=0;mode<5;++mode){
  Context c{p.get_ptr(),new display_list,mode};
  for(int i=0;i<count;++i)add(c,i,i+1);
  c.list->advance(1.0f/30);
  std::vector<int> expected;
  if(count&&mode==3){expected.push_back(0);for(int i=0;i<count;++i)expected.push_back(i);for(int i=1;i<count;++i)expected.push_back(i);}
  else for(int i=0;i<count;++i)expected.push_back(i);
  if(count&&(mode==1||mode==2))for(int i=0;i<count;++i)expected.push_back(1000+i);
  if(count&&mode==4)expected.push_back(1000+count-1);
  check(c.events==expected);
  if(count&&mode==1){check(c.list->size()==1);c.list->advance(1.0f/30);check(c.events.back()==900);}
  if(c.list){c.list->clear();delete c.list;c.list=nullptr;}
  // Every initial actor is released once, after its last snapshot use.
  for(int id=0;id<count;++id){unsigned released=0;for(int e:c.events)released+=e==1000+id;check(released==1);}
 }
 std::printf("{\"checks\":%u,\"pass\":true}\n",checks);
}
