#pragma once
#include <cstdint>
#include <cstdio>
namespace dephase119 {
constexpr unsigned OffsetUs=4000;
inline unsigned wait_us(uint64_t now,uint64_t last_present,bool have_scene,bool endpoint_due,bool pending,bool loading,bool help,bool source_done){
 if(!have_scene||endpoint_due||pending||loading||help||source_done||!last_present||now<last_present||last_present>~uint64_t(0)-OffsetUs)return 0;
 uint64_t elapsed=now-last_present;return elapsed<OffsetUs?unsigned(OffsetUs-elapsed):0;
}
struct Stats{
 unsigned waits=0,ready_after_wait=0,endpoint_delays=0,loading_delays=0,maximum_requested=0;
 uint64_t requested_us=0;
 void record(unsigned requested,bool ready,bool endpoint,bool loading){++waits;ready_after_wait+=ready;endpoint_delays+=endpoint;loading_delays+=loading;requested_us+=requested;if(requested>maximum_requested)maximum_requested=requested;}
 bool write(const char* path)const{FILE*f=std::fopen(path,"w");if(!f)return false;std::fprintf(f,"{\"scope\":\"Renderthread only,hold candidates without pending scene or endpoint;no sourceclock changes\",\"offset_us\":%u,\"waits\":%u,\"ready_after_wait\":%u,\"endpoint_delays\":%u,\"loading_delays\":%u,\"maximum_requested_us\":%u,\"requested_us\":%llu}\n",OffsetUs,waits,ready_after_wait,endpoint_delays,loading_delays,maximum_requested,(unsigned long long)requested_us);return std::fclose(f)==0;}
};
}
