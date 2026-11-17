// Host-only, read-only drain of immutable PSP presentation records. Count is
// committed after each record; the second count check detects any overwrite.
typedef struct {uint32_t flags,vcount;uint64_t time_us;} HostPresentRecord;
typedef struct {
    FILE* frames;FILE* loads;uint32_t address,loading_address,next,last_load;
    uint64_t failures,overruns,discarded,records;bool ready;
} RingReader;
static bool ring_copy(uint8_t* base,uint32_t address,size_t size,void* destination){
    if(!base||address<0x08000000u||(uint64_t)address+size>0x0c000000u)return false;
    mach_vm_size_t copied=0;
    return mach_vm_read_overwrite(mach_task_self(),(mach_vm_address_t)(base+address),size,(mach_vm_address_t)destination,&copied)==KERN_SUCCESS&&copied==size;
}
static RingReader ring_open(void){
    RingReader r={0};const char* address=getenv("PSP_PRESENT_RING_ADDRESS");
    if(!address)return r;
    r.address=(uint32_t)strtoul(address,NULL,0);
    const char* loading=getenv("PSP_LOADING_ADDRESS");
    if(loading)r.loading_address=(uint32_t)strtoul(loading,NULL,0);
    const char* path=getenv("PSP_PRESENT_RING_OUTPUT");
    if(path)r.frames=fopen(path,"wx");
    if(r.frames){setvbuf(r.frames,NULL,_IOFBF,65536);fputs("presentation,source_tick,subframe,fresh,has_motion,vcount,time_us\n",r.frames);fflush(r.frames);}
    path=getenv("PSP_PRESENT_LOAD_OUTPUT");
    if(path)r.loads=fopen(path,"wx");
    if(r.loads){fputs("id,source_tick,reason,requested_us,shown_us,visible,shown_vcount,init_begin_us,ready_us,first_submit_us,resumed_us,resume_vcount\n",r.loads);fflush(r.loads);}
    if(!r.frames||!r.loads)++r.failures;
    return r;
}
static void ring_poll(RingReader* r,uint8_t* base){
    if(!r->address||!r->frames)return;
    uint32_t descriptor[4];
    if(!ring_copy(base,r->address,sizeof(descriptor),descriptor))return;
    if(descriptor[0]!=0x31474e52u)return;
    uint32_t count=descriptor[1],capacity=descriptor[2],address=descriptor[3];
    if(capacity<64||capacity>4096){++r->failures;return;}
    r->ready=true;
    if(count<r->next){++r->failures;return;}
    if(count-r->next>capacity){++r->overruns;r->discarded+=count-r->next-capacity;r->next=count-capacity;}
    uint32_t begin=r->next,n=count-begin;
    if(n){
        HostPresentRecord rows[4096];uint32_t start=begin%capacity,first=n<capacity-start?n:capacity-start;
        bool copied=ring_copy(base,address+start*sizeof(*rows),first*sizeof(*rows),rows);
        if(copied&&n>first)copied=ring_copy(base,address,(n-first)*sizeof(*rows),rows+first);
        uint32_t after[4];
        if(!copied||!ring_copy(base,r->address,sizeof(after),after)){++r->failures;return;}
        if(after[0]!=descriptor[0]||after[2]!=capacity||after[3]!=address||after[1]<count){++r->failures;return;}
        if(after[1]-begin>capacity){++r->overruns;return;}
        for(uint32_t i=0;i<n;i++){
            HostPresentRecord x=rows[i];
            fprintf(r->frames,"%u,%u,%u,%u,%u,%u,%llu\n",begin+i+1,x.flags>>3,(x.flags>>1)&1,x.flags&1,(x.flags>>2)&1,x.vcount,(unsigned long long)x.time_us);
        }
        r->next=count;r->records+=n;fflush(r->frames);
    }
    uint32_t pointer=0;uint8_t rows[640];
    if(!r->loading_address||!r->loads||!ring_copy(base,r->loading_address,4,&pointer)||!ring_copy(base,pointer,sizeof(rows),rows))return;
    // ABI is asserted in the PSP frontend: Row80, timestamps24..64, visible72.
    for(unsigned k=0;k<8;k++){
        uint8_t* row=rows+k*80;uint32_t word[5];uint64_t t[6];
        memcpy(word,row,sizeof(word));memcpy(t,row+24,sizeof(t));
        if(word[0]<=r->last_load||!word[4]||!row[72])continue;
        if(!(t[0]<=t[1]&&t[1]<=t[2]&&t[2]<=t[3]&&t[3]<=t[4]&&t[4]<=t[5])||word[4]<word[3])continue;
        fprintf(r->loads,"%u,%u,%u,%llu,%llu,1,%u,%llu,%llu,%llu,%llu,%u\n",word[0],word[1],word[2],(unsigned long long)t[0],(unsigned long long)t[1],word[3],(unsigned long long)t[2],(unsigned long long)t[3],(unsigned long long)t[4],(unsigned long long)t[5],word[4]);
        r->last_load=word[0];fflush(r->loads);
    }
}
static void ring_close(RingReader* r,uint8_t* base){
    if(!r->address)return;
    ring_poll(r,base);if(r->frames)fclose(r->frames);if(r->loads)fclose(r->loads);
    const char* path=getenv("PSP_PRESENT_RING_SUMMARY");FILE* file=path?fopen(path,"w"):NULL;
    if(file){fprintf(file,"{\"status\":\"recorded\",\"backend\":\"read-only committed PSP RAM ring, host disk only\",\"descriptor_ready\":%s,\"records\":%llu,\"last_presentation\":%u,\"read_failures\":%llu,\"overruns\":%llu,\"discarded_records\":%llu,\"last_loading_id\":%u}\n",r->ready?"true":"false",(unsigned long long)r->records,r->next,(unsigned long long)r->failures,(unsigned long long)r->overruns,(unsigned long long)r->discarded,r->last_load);fclose(file);}
}
