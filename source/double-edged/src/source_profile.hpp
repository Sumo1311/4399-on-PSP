#pragma once

// Opt-in, source-thread-owned RAM telemetry. No per-tick file writes and no
// allocation in ordinary play. The main thread reads only after source_done.
struct SourceSample {
    unsigned tick,logic_us,capture_us,gc_us,complete_us,heap_bytes;
    uint64_t sample_us=0,scheduled_us=0;
    unsigned gc_before=0,gc_after=0,gc_reason=0,captured=1;
};
struct SourceCapture {
    std::unique_ptr<SourceSample[]> rows;
    unsigned used=0,capacity=0;
    bool overflow=false;
    bool init(bool enabled,unsigned limit){
        if(!enabled)return true;
        if(!limit||limit>65536)return false;
        capacity=limit;rows.reset(new(std::nothrow) SourceSample[capacity]);
        return bool(rows);
    }
    void append(const SourceSample& sample){
        if(!rows)return;
        if(used==capacity){overflow=true;return;}
        rows[used++]=sample;
    }
    bool write()const{
        if(!rows)return true;
        FILE* file=std::fopen("SOURCE_CPU.CSV","w");if(!file)return false;
        std::setvbuf(file,nullptr,_IOFBF,32768);
        bool valid=std::fputs("source_tick,logic_us,capture_us,gc_us,complete_us,heap_bytes,sample_us,scheduled_us,gc_before,gc_after,gc_reason,captured\n",file)>=0;
        for(unsigned i=0;i<used&&valid;++i){auto& r=rows[i];
            valid=std::fprintf(file,"%u,%u,%u,%u,%u,%u,%llu,%llu,%u,%u,%u,%u\n",r.tick,r.logic_us,r.capture_us,r.gc_us,r.complete_us,r.heap_bytes,(unsigned long long)r.sample_us,(unsigned long long)r.scheduled_us,r.gc_before,r.gc_after,r.gc_reason,r.captured)>0;
        }
        return std::fclose(file)==0&&valid&&!overflow;
    }
};
