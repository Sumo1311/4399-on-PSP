// Uses ring_copy from the existing committed presentation reader.
typedef struct {
    uint32_t tick,buttons,queued,subframe,fresh,phase;
    uint32_t logic_us,capture_us,draw_us,submit_us,worker_wait_us,source_tick_now;
    uint64_t sampled_us,published_us,presented_us;
} HostResponseRecord;
static_assert(sizeof(HostResponseRecord)==72,"PSP/host response ABI");
typedef struct {
    FILE* file; uint32_t address,next; uint64_t failures,overruns,records; bool ready;
} ResponseReader;
static ResponseReader response_open(void){
    ResponseReader r={0};const char* a=getenv("PSP_RESPONSE_RING_ADDRESS");
    if(!a)return r;
    r.address=(uint32_t)strtoul(a,NULL,0);
    const char* path=getenv("PSP_RESPONSE_OUTPUT");if(path)r.file=fopen(path,"wx");
    if(r.file){setvbuf(r.file,NULL,_IOFBF,65536);fputs("record,tick,buttons,queued,subframe,fresh,phase,logic_us,capture_us,draw_us,submit_us,worker_wait_us,source_tick_now,sampled_us,published_us,presented_us\n",r.file);fflush(r.file);}
    return r;
}
static void response_poll(ResponseReader* r,uint8_t* base){
    if(!r->address||!r->file)return;
    uint32_t h[4];if(!ring_copy(base,r->address,sizeof(h),h)||h[0]!=0x31505352)return;
    r->ready=true;uint32_t count=h[1],capacity=h[2],address=h[3];
    if(capacity!=512||count<r->next){++r->failures;return;}
    if(count-r->next>capacity){++r->overruns;r->next=count-capacity;}
    uint32_t begin=r->next,n=count-begin;if(!n)return;
    HostResponseRecord rows[512];uint32_t start=begin%capacity,first=n<capacity-start?n:capacity-start;
    bool ok=ring_copy(base,address+start*sizeof(*rows),first*sizeof(*rows),rows);
    if(ok&&n>first)ok=ring_copy(base,address,(n-first)*sizeof(*rows),rows+first);
    uint32_t after[4];
    if(!ok||!ring_copy(base,r->address,sizeof(after),after)||after[0]!=h[0]||after[2]!=capacity||after[3]!=address||after[1]<count){++r->failures;return;}
    if(after[1]-begin>capacity){++r->overruns;return;}
    for(unsigned i=0;i<n;++i){HostResponseRecord x=rows[i];
        fprintf(r->file,"%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%llu,%llu,%llu\n",begin+i+1,x.tick,x.buttons,x.queued,x.subframe,x.fresh,x.phase,x.logic_us,x.capture_us,x.draw_us,x.submit_us,x.worker_wait_us,x.source_tick_now,(unsigned long long)x.sampled_us,(unsigned long long)x.published_us,(unsigned long long)x.presented_us);
    }
    r->next=count;r->records+=n;fflush(r->file);
}
static void response_close(ResponseReader* r,uint8_t* base){
    if(!r->address)return;response_poll(r,base);if(r->file)fclose(r->file);
    const char* p=getenv("PSP_RESPONSE_SUMMARY");FILE* f=p?fopen(p,"w"):NULL;
    if(f){fprintf(f,"{\"ready\":%s,\"records\":%llu,\"read_failures\":%llu,\"overruns\":%llu}\n",r->ready?"true":"false",(unsigned long long)r->records,(unsigned long long)r->failures,(unsigned long long)r->overruns);fclose(f);}
}
