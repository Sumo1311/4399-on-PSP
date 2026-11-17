#include "gameswf/gameswf_player.h"
#include "gameswf/gameswf_character.h"
#include "gameswf/gameswf_dlist.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static unsigned checks=0;
static void check(bool ok){++checks;if(!ok){std::fprintf(stderr,"lookup mismatch at check %u\n",checks);std::exit(1);}}
struct Collisions { size_t operator()(const tu_stringi& key)const {return key.c_str()[0]=='s'?size_t(-1):7;} };
template<class F> static void hash_test(){
    hash<tu_stringi,int,F> table;
    std::vector<tu_stringi> keys;
    for(int i=0;i<300;++i)keys.emplace_back(((i%2?"sample":"Key")+std::to_string(i)).c_str());
    std::vector<Uint64> epochs(keys.size(),0);std::vector<int> indices(keys.size(),-1);
    auto verify=[&](){
        const auto& const_table=table;
        for(unsigned k=0;k<keys.size();++k){auto& key=keys[k];
            auto a=table.find(key),b=table.find_prehashed(key,F()(key));
            auto cached=table.find_prehashed_cached(key,F()(key),epochs[k],indices[k]);
            check((a==table.end())==(cached==table.end()));if(a!=table.end())check(a->second==cached->second);
            auto c=const_table.find_prehashed(key,F()(key));
            check((a==table.end())==(b==table.end()));check((a==table.end())==(c==const_table.end()));
            if(a!=table.end()){check(a->second==b->second);check(a->second==c->second);}
        }
    };
    verify();
    for(int i=0;i<300;++i){table.set(keys[i],i);if(i%23==0)verify();}
    for(int i=0;i<300;i+=2)table.erase(keys[i]);verify();
    for(int i=0;i<300;i+=3)table.set(keys[i],1000+i);verify();
    table.set_capacity(2048);verify();table.set_capacity(table.size());verify();
    auto copied=table;table.clear();verify();table=copied;verify();
    for(int i=0;i<300;++i){auto s=std::string(keys[i].c_str());for(char& c:s)if(c>='A'&&c<='Z')c+='a'-'A';tu_stringi key(s.c_str());
        auto a=table.find(key),b=table.find_prehashed(key,F()(key));check((a==table.end())==(b==table.end()));if(a!=table.end())check(a->second==b->second);}
    for(auto& key:keys)table.erase(key);verify();
}
int main(){
    hash_test<stringi_hash_functor<tu_stringi>>();hash_test<Collisions>();
    gameswf::gc_ptr<gameswf::player> player=new gameswf::player;
    gameswf::display_list list;
    std::vector<gameswf::gc_ptr<gameswf::character>> owned;
    std::vector<std::string> names={"", "kid1","KID1","hero","Hat","SHIRT","missing","new_name", "\xc3\x89", "\xc3\xa9"};
    for(int i=0;i<80;++i)names.push_back("name_"+std::to_string(i));
    auto verify=[&](){
        for(auto& text:names){tu_stringi name(text.c_str());gameswf::character* expected=nullptr;
            for(int i=0;i<list.size();++i)if(name==list.get_character(i)->get_name().c_str()){expected=list.get_character(i);break;}
            check(list.get_character_by_name_i(name)==expected);
        }
    };
    auto add=[&](const char* name,int depth,bool replace){
        gameswf::gc_ptr<gameswf::character> ch=new gameswf::character(player.get_ptr(),nullptr,int(owned.size()+1));
        ch->set_name(name);owned.push_back(ch);
        list.add_display_object(ch.get_ptr(),depth,replace,gameswf::cxform(),gameswf::matrix(),0,0,0);
    };
    auto swap=[&](gameswf::character* a,gameswf::character* b){int depth=a->get_depth();a->set_depth(b->get_depth());b->set_depth(depth);list.swap_characters(a,b);};
    verify();for(int i=0;i<30;++i){add(names[i%names.size()].c_str(),i+1,true);verify();}
    // Same size replacement, renaming, duplicate priority, and reordering.
    add("new_name",12,true);verify();
    list.get_character(0)->set_name("new_name");verify();
    list.get_character(0)->set_name("\xc3\xa9");verify();
    list.get_character(0)->set_name("kid1");verify();
    swap(list.get_character(0),list.get_character(20));verify();
    list.change_character_depth(list.get_character(0),100);verify();
    list.clear();
    for(int i=0;i<24;++i)add(names[10+i].c_str(),i+1,true);
    for(int i=0;i<5;++i)verify();
    add("kid1",25,true);add("KID1",26,true);verify();verify();
    swap(list.get_character(0),list.get_character(25));verify();verify();
    list.get_character(0)->set_name("new_name");verify();verify();
    list.get_character(0)->set_name("\xc3\x89");verify();
    list.get_character(0)->set_name("hero");verify();verify();
    add("new_name",14,true);verify();verify();
    unsigned random=0x185d9;
    for(int step=0;step<1200;++step){
        random=random*1664525u+1013904223u;unsigned op=random%7;
        unsigned index=list.size()?(random>>8)%list.size():0;
        const char* name=names[(random>>16)%names.size()].c_str();
        if(op<2||!list.size())add(name,1+(random>>5)%100,true);
        else if(op==2)list.get_character(index)->set_name(name);
        else if(op==3)list.remove_display_object(list.get_character(index));
        else if(op==4)swap(list.get_character(index),list.get_character((index+1)%list.size()));
        else if(op==5)list.change_character_depth(list.get_character(index),1000+step);
        else {array<int> depths;for(int i=0;i<list.size();i+=2)depths.push_back(list.get_character(i)->get_depth());list.clear_unaffected(depths);}
        verify();
    }
    list.clear();verify();
    // Check every first byte against the original C-string comparison. Keep
    // declared data after NUL, since empty prefixes must retain that behavior.
    add("",1,true);
    for(unsigned child_byte=0;child_byte<256;++child_byte){
        tu_string child;child.resize(3);child[0]=char(child_byte);child[1]='x';child[2]='\0';
        list.get_character(0)->set_name(child);
        for(unsigned query_byte=0;query_byte<256;++query_byte){
            tu_stringi query;query.to_tu_string().resize(3);
            query[0]=char(query_byte);query[1]='x';query[2]='\0';
            auto expected=query==list.get_character(0)->get_name().c_str()?list.get_character(0):nullptr;
            check(list.get_character_by_name_i(query)==expected);
        }
    }
    list.clear();
    // Match the large named world list, including duplicates and invalidation.
    for(unsigned i=0;i<400;++i)add(("static"+std::to_string(i)).c_str(),10+i,true);
    add("STATIC123",1,false);add("",410,true);
    auto verify_large=[&](){
        for(unsigned i=0;i<800;++i){
            tu_stringi query(("STATIC"+std::to_string(i)).c_str());
            gameswf::character* expected=nullptr;
            for(int n=0;n<list.size();++n)if(query==list.get_character(n)->get_name().c_str()){expected=list.get_character(n);break;}
            check(list.get_character_by_name_i(query)==expected);
        }
    };
    verify_large();
    list.get_character(0)->set_name("renamed");verify_large();
    list.get_character(1)->set_name("\xc3\x89");verify_large();
    list.get_character(1)->set_name("static0");verify_large();
    swap(list.get_character(0),list.get_character(200));verify_large();
    list.clear();auto stats=gameswf::dnm_get_name_lookup_stats();
    check(stats.lookups>100000);check(stats.rejects>0);check(stats.rebuilds>0);check(stats.hits>0);
    std::printf("{\"checks\":%u,\"lookups\":%llu,\"rejected\":%llu,\"scanned\":%llu,\"rebuilds\":%llu,\"positive_hits\":%llu,\"pass\":true}\n",checks,stats.lookups,stats.rejects,stats.scanned,stats.rebuilds,stats.hits);
}
