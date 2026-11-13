// Included inside the actual Game translation unit, diagnostic target only.
// Compare complete live frames, with the normal HUD, map, audio and cache.
alignas(64) static std::array<std::uint32_t,512*272> liveDirectPixels,liveQueuedPixels;

bool Game::liveRenderProbe(const std::string& reportFolder) {
    auto& qa=g_liveRenderQa;
    const std::string drive=reportFolder.starts_with("ef0:")?"ef0:":"ms0:";
    const int file=sceIoOpen((reportFolder+"LIVE_RENDER_REPORT.TXT").c_str(),PSP_O_WRONLY|PSP_O_CREAT|PSP_O_TRUNC,0777);
    if(file<0){qa.error=1;return false;}
    const auto log=[&](const char* format,...) {
        char line[768];va_list args;va_start(args,format);
        const int n=std::vsnprintf(line,sizeof(line),format,args);va_end(args);
        if(n>0)sceIoWrite(file,line,std::min(n,int(sizeof(line)-1)));
    };
    const auto flush=[&]{sceIoSync(drive.c_str(),0);};
    log("BVN37 LIVE RENDER CHECK 2\nfirmware=0x%08x\nreader=stdio-only\nscope=actual Game rendering, source gameplay, stage, HUD, audio and assists\nreference=original GU_DIRECT; candidate=completed GU_SEND lists\nfixture=training resource refill; paired frames do not advance simulation\n",sceKernelDevkitVersion());flush();
    const auto writeImage=[&](const char* suffix,const std::array<std::uint32_t,512*272>& source) {
        const auto path=reportFolder+"SCENE"+std::to_string(qa.scene)+"_"+suffix+".PPM";
        FILE* image=std::fopen(path.c_str(),"wb");if(!image)return;
        std::fprintf(image,"P6\n480 272\n255\n");
        const auto* pixels=reinterpret_cast<const volatile unsigned*>(reinterpret_cast<std::uintptr_t>(source.data())|0x40000000u);
        std::array<unsigned char,480*3> row;
        for(unsigned y=0;y<272;++y){for(unsigned x=0;x<480;++x){const auto p=pixels[y*512+x];row[x*3]=p&255;row[x*3+1]=(p>>8)&255;row[x*3+2]=(p>>16)&255;}std::fwrite(row.data(),1,row.size(),image);}
        std::fclose(image);
    };
    struct Case {const char* player;const char* rival;const char* assist;const char* map;unsigned quality;};
    const Case cases[]{
        {"ITACHI.BVF","NARUTO.BVF","AGRIM.BVS","ZOULANG.BVN",0},
        {"GIN.BVF","NARUTO1.BVF","AGRIM.BVS","ZOULANG.BVN",0},
        {"HINATA.BVF","BYAKUYB.BVF","AINO.BVS","SENLIN.BVN",0},
        {"ITACHI.BVF","NARUTO.BVF","AGRIM.BVS","ZOULANG.BVN",3},
    };
    const auto choose=[&](const RosterAsset& roster,const char* name,FighterPick& pick) {
        for(unsigned cell=0;cell<roster.cells().size();++cell)for(unsigned alt=0;alt<=roster.cells()[cell].moreCount;++alt){
            const auto id=roster.cells()[cell].choice(alt);
            if(id<roster.size()&&std::strcmp(roster.fighter(id).file,name)==0){pick={cell,alt};return true;}
        }
        return false;
    };
    bool complete=true;
    for(unsigned scene=0;scene<std::size(cases)&&complete;++scene) {
        qa.scene=scene;const auto& c=cases[scene];
        clearBattle();mode_=BattleMode::Training;arcadeEncounter_=nullptr;
        settings_=GameSettings{};settings_.quality=c.quality;applySettings();
        teamCount_[0]=teamCount_[1]=0;
        if(!choose(roster_,c.player,picks_[0])||!choose(roster_,c.rival,picks_[1])||
           !choose(assistRoster_,c.assist,assistPicks_[0])||!choose(assistRoster_,"ASAKURA.BVS",assistPicks_[1])){qa.error=2;complete=false;break;}
        const auto map=std::ranges::find(maps_,std::string(c.map),&MapChoice::file);
        if(map==maps_.end()){qa.error=3;complete=false;break;}map_=unsigned(map-maps_.begin());
        log("SCENE START %u player=%s rival=%s assist=%s map=%s quality=%u\n",scene,c.player,c.rival,c.assist,c.map,c.quality);flush();
        beginFight();
        if(!round_||screen_!=Screen::Fight){qa.error=4;complete=false;break;}
        unsigned previous=0,play=0,scenePairs=0;bool savedDifference=false;
        for(unsigned tick=0;tick<1800&&play<360;++tick) {
            qa.step=play;const bool playing=round_->phase()==RoundPhase::Playing;
            unsigned held=0;
            if(playing) {
                if(play<24)held=PSP_CTRL_DOWN;
                else if(play<48)held=PSP_CTRL_DOWN|PSP_CTRL_CIRCLE;
                else if(play<88)held=PSP_CTRL_RIGHT;
                else if(play<112)held=PSP_CTRL_SQUARE;
                else if(play<136)held=PSP_CTRL_DOWN|PSP_CTRL_SQUARE;
                else if(play<160)held=PSP_CTRL_CIRCLE;
                else if(play<184)held=PSP_CTRL_TRIANGLE;
                else if(play<224)held=PSP_CTRL_RTRIGGER;
                else if(play<264)held=PSP_CTRL_DOWN|PSP_CTRL_RTRIGGER;
                else if(play<304)held=PSP_CTRL_UP|PSP_CTRL_RTRIGGER;
                else if(play<336)held=PSP_CTRL_LTRIGGER;
                if(play%24==0){auto& actor=const_cast<FighterMotion&>(round_->match().actor(0));actor.setQi(actor.maxQi());actor.setEnergy(actor.maxEnergy());}
            }
            step(held,held&~previous);previous=held;
            if(!round_||screen_!=Screen::Fight){qa.error=5;complete=false;break;}
            const bool paired=playing&&(play<24||play%8==0);
            if(paired) {
                renderer_.deferred(false);draw();
                if(!renderer_.readback(liveDirectPixels)){qa.error=6;complete=false;break;}
                renderer_.deferred(true);draw();
                if(!renderer_.readback(liveQueuedPixels)){qa.error=7;complete=false;break;}
                const auto* a=reinterpret_cast<const volatile unsigned*>(reinterpret_cast<std::uintptr_t>(liveDirectPixels.data())|0x40000000u);
                const auto* b=reinterpret_cast<const volatile unsigned*>(reinterpret_cast<std::uintptr_t>(liveQueuedPixels.data())|0x40000000u);
                unsigned different=0,blackDirect=0,blackQueued=0,maxError=0,firstX=0,firstY=0,firstA=0,firstB=0;
                for(unsigned y=0;y<272;++y)for(unsigned x=0;x<480;++x) {
                    const unsigned av=a[y*512+x]&0xffffff,bv=b[y*512+x]&0xffffff;
                    if(x<59||x>=422)qa.pillarPixels+=unsigned(av!=0)+unsigned(bv!=0);
                    unsigned error=0;for(unsigned ch=0;ch<3;++ch)error=std::max(error,unsigned(std::abs(int(av>>(ch*8)&255)-int(bv>>(ch*8)&255))));
                    if(error>3){if(!different){firstX=x;firstY=y;firstA=av;firstB=bv;}++different;blackDirect+=!av&&bv;blackQueued+=!bv&&av;}
                    maxError=std::max(maxError,error);
                }
                ++qa.pairs;++scenePairs;qa.pixels+=480*272;qa.differingPixels+=different;qa.directBlack+=blackDirect;qa.queuedBlack+=blackQueued;qa.maxError=std::max(qa.maxError,maxError);
                if(play<24)++qa.guardFrames;
                const auto effectCount=round_->match().scene().combat.size();if(effectCount)++qa.complexFrames;
                log("FRAME scene=%u step=%u timeline=%u effects=%u different=%u direct_black=%u queued_black=%u max=%u first=%u,%u:%06x,%06x\n",scene,play,round_->match().actor(0).timelineFrame(),unsigned(effectCount),different,blackDirect,blackQueued,maxError,firstX,firstY,firstA,firstB);
                if(play==8){writeImage("GUARD_DIRECT",liveDirectPixels);writeImage("GUARD_QUEUED",liveQueuedPixels);}
                if(play==168){writeImage("EFFECT_DIRECT",liveDirectPixels);writeImage("EFFECT_QUEUED",liveQueuedPixels);}
                if(different&&!savedDifference){writeImage("DIFF_DIRECT",liveDirectPixels);writeImage("DIFF_QUEUED",liveQueuedPixels);savedDifference=true;}
            } else {renderer_.deferred(true);draw();}
            if(playing)++play;
        }
        log("SCENE END %u play_steps=%u pairs=%u\n",scene,play,scenePairs);flush();
        if(play!=360){qa.error=8;complete=false;}
    }
    log("RESULT complete=%u error=%u pairs=%u pixels=%u differing=%u direct_black=%u queued_black=%u max_error=%u guards=%u complex_frames=%u pillar_pixels=%u\n",unsigned(complete),qa.error,qa.pairs,qa.pixels,qa.differingPixels,qa.directBlack,qa.queuedBlack,qa.maxError,qa.guardFrames,qa.complexFrames,qa.pillarPixels);
    log("GE lists=%u max_bytes=%u submit_errors=%u queued_lists=%u\n",unsigned(g_geSubmissionQa.lists),unsigned(g_geSubmissionQa.maxBytes),unsigned(g_geSubmissionQa.submitErrors),unsigned(g_geSubmissionQa.deferredLists));
    sceIoClose(file);flush();renderer_.deferred(true);clearBattle();
    return complete;
}
