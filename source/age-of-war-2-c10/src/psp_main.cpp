#include "game.hpp"
#include "gfx.hpp"
#include "ui.hpp"
#include "audio.hpp"
#include "boot_trace.hpp"
#include "qa_replay.hpp"
#include "ui_probe.hpp"
#include <pspkernel.h>
#include <pspctrl.h>
#include <psprtc.h>
#include <psppower.h>
#include <malloc.h>
#include <cstdio>
#include <algorithm>
#include <cmath>
PSP_MODULE_INFO("AgeOfWar2PSP",0,1,3);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER|THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(256);
// The early CRT hook selects an available contiguous heap before global constructors.
PSP_HEAP_SIZE_KB(-1);
PSP_HEAP_THRESHOLD_SIZE_KB(1024);
namespace {
volatile bool running=true,suspendRequested=false,resumed=false;
aow::Game saveSnapshot;volatile bool saveBusy=false,saveRunning=true;SceUID saveWake=-1,saveThread=-1;
int backgroundSave(SceSize,void*){while(true){sceKernelWaitSema(saveWake,1,nullptr);if(!saveRunning)break;saveSnapshot.save("SUSPEND.DAT");saveBusy=false;}return 0;}
void waitForSave(){while(saveBusy)sceKernelDelayThread(1000);}
void queueSave(const aow::Game& game){if(saveThread<0){game.save("SUSPEND.DAT");return;}if(saveBusy)return;saveSnapshot=game;saveBusy=true;sceKernelSignalSema(saveWake,1);}
int exitCallback(int,int,void*){running=false;return 0;}
int powerCallback(int,int flags,void*){if(flags&PSP_POWER_CB_SUSPENDING)suspendRequested=true;if(flags&PSP_POWER_CB_RESUME_COMPLETE)resumed=true;return 0;}
int callbackThread(SceSize,void*){sceKernelRegisterExitCallback(sceKernelCreateCallback("aow_exit",exitCallback,nullptr));scePowerRegisterCallback(0,sceKernelCreateCallback("aow_power",powerCallback,nullptr));sceKernelSleepThreadCB();return 0;}
double clockSeconds(){uint64_t tick;sceRtcGetCurrentTick(&tick);return double(tick)/sceRtcGetTickResolution();}
}
int main() {
 using namespace aow;
 bootTrace("main-entered");
 SceUID cb=sceKernelCreateThread("aow_callbacks",callbackThread,0x11,4096,0,nullptr);sceKernelStartThread(cb,0,nullptr);
 scePowerSetClockFrequency(333,333,166);
 saveWake=sceKernelCreateSema("aow_save_wake",0,0,1,nullptr);if(saveWake>=0)saveThread=sceKernelCreateThread("aow_save",backgroundSave,0x31,64*1024,PSP_THREAD_ATTR_USER,nullptr);if(saveThread>=0&&sceKernelStartThread(saveThread,0,nullptr)<0){sceKernelDeleteThread(saveThread);saveThread=-1;}
 sceCtrlSetSamplingCycle(0);sceCtrlSetSamplingMode(PSP_CTRL_MODE_DIGITAL);
 bootTrace("graphics-init");boot10_release_display();Gfx::init();bootTrace("graphics-ready");
 Gfx::begin();Gfx::clip(false);Gfx::rect(0,0,480,272,0xff222824);Gfx::text("音效  Loading...",178,128,0xffd9e7ee);bootTrace("loading-submit");Gfx::end();bootTrace("loading-presented");
 bootTrace("audio-init");bool audioReady=Audio::init();bootTrace(audioReady?"audio-ready":"audio-failed");Audio::music("Music2");
 Game game;UI ui;if(!audioReady)ui.notify("音效不可用");bootTrace("menu-ready");Replay replay;bool qa=replay.read("QA_INPUT.txt");bool qaFast=qa&&(replay.caseId=="mechanics-sweep"||replay.caseId.rfind("full-route-",0)==0);FILE* trace=nullptr;int replayTick=0;
 if(FILE* realTime=std::fopen("QA_REALTIME.txt","rb")){std::fclose(realTime);qaFast=false;}
 if(qa){game.reset(replay.difficulty,replay.seed);game.paused=game.tutorial=false;if(replay.caseId=="mechanics-sweep"||replay.caseId=="full-route-victory-check")game.aiEnabled=false;ui.page=UI::Battle;trace=std::fopen("QA_TRACE.tsv","wb");Audio::music("Music1");}
 FILE* optIn=std::fopen("QA_PERF.txt","rb");bool profiling=qa||optIn;if(optIn)std::fclose(optIn);
 FILE* uiFlag=std::fopen("QA_UI.txt","rb");FILE* uiTrace=nullptr;if(uiFlag){std::fclose(uiFlag);uiTrace=std::fopen("QA_UI.tsv","wb");}
 FILE* perf=profiling?std::fopen("PERF.csv","wb"):nullptr;
 if(perf){std::setvbuf(perf,nullptr,_IOFBF,32768);std::fprintf(perf,"seconds,logic_ticks,draw_ms,texture_bytes,audio_bytes,heap_used,heap_free,kernel_free,texture_misses,disk_reads,disk_bytes,prefetches,cpu_mhz,bus_mhz,pinned_ui_bytes,gu_submissions,gu_peak_list_bytes\n");}
 FILE* frames=profiling?std::fopen("PRESENT60.CSV","w"):nullptr;
 if(frames){std::setvbuf(frames,nullptr,_IOFBF,65536);std::fputs("presentation,logic_tick,vcount,time_us,texture_misses,texture_bytes\n",frames);}
 double last=clockSeconds(),accumulator=0,lastPerf=last,lastSave=last,nextDraw=last,lastUi=last;unsigned previous=0;double held=0;unsigned heldButton=0;
 size_t peakHeap=0;int rendered=0;
 while(running&&!ui.wantExit) {
  double now=clockSeconds(),dt=now-last;last=now;
  if(resumed){resumed=false;scePowerSetClockFrequency(333,333,166);game.paused=true;accumulator=0;dt=0;ui.notify("游戏已暂停");}
  if(suspendRequested){suspendRequested=false;if(ui.page==UI::Battle){waitForSave();game.paused=true;game.save("SUSPEND.DAT");}}
  SceCtrlData pad{};sceCtrlPeekBufferPositive(&pad,1);unsigned changed=pad.Buttons^previous;unsigned pressed=pad.Buttons&~previous;
  unsigned directions=pad.Buttons&(PSP_CTRL_UP|PSP_CTRL_DOWN|PSP_CTRL_LEFT|PSP_CTRL_RIGHT);
  if(directions!=heldButton){held=0;heldButton=directions;}else held+=dt;
  if(directions&&held>.32){pressed|=directions;held=.24;}
  if(!qa) {
   if(pressed&PSP_CTRL_UP)ui.move(game,0,-1);if(pressed&PSP_CTRL_DOWN)ui.move(game,0,1);
   if(pressed&PSP_CTRL_LEFT)ui.move(game,-1,0);if(pressed&PSP_CTRL_RIGHT)ui.move(game,1,0);
   if(pressed&PSP_CTRL_CROSS){waitForSave();ui.confirm(game);}if(pressed&PSP_CTRL_CIRCLE)ui.back(game);
   if(pressed&PSP_CTRL_TRIANGLE)ui.triangle(game);if(pressed&PSP_CTRL_SQUARE){waitForSave();ui.square(game);}
   if(pressed&PSP_CTRL_START)ui.start(game);if(pressed&PSP_CTRL_SELECT)ui.help(game);
   if(ui.page==UI::Battle&&!game.paused) {
    if(pad.Buttons&PSP_CTRL_LTRIGGER)game.camera=std::min(900.,game.camera+900*dt);
    if(pad.Buttons&PSP_CTRL_RTRIGGER)game.camera=std::max(-180.,game.camera-900*dt);

   }
  }
  previous=pad.Buttons;ui.update(dt);
  if(ui.page==UI::Battle && (!qa || replayTick<replay.total)) {
   accumulator+=qaFast?LOGIC_DT*200:dt;
   int steps=0;while(accumulator>=LOGIC_DT&&(qaFast||steps++<8)) {
    if(qa)replay.apply(game,replayTick);
    game.update(LOGIC_DT*game.speed);accumulator-=LOGIC_DT;
    if(qa){if(trace && (replayTick%replay.every==0||(replay.caseId.rfind("full-route-",0)==0&&replayTick==replay.total-1))){auto row=sampleRow(game,replayTick);std::fprintf(trace,"%s\n",row.c_str());std::fflush(trace);}++replayTick;if(replayTick>=replay.total){if(trace){std::fclose(trace);trace=nullptr;}game.save("QA_FINAL.DAT");break;}}
   }
  } else accumulator=0;
  for(auto& sound:game.sounds)if(!qaFast){double x=sound.x+game.camera;float v=std::max(0.,std::min(1.,(360-std::abs(x-360))/300));Audio::effect(sound.name,v);}game.sounds.clear();
  if(!qa&&ui.page==UI::Battle&&!game.paused&&now-lastSave>30){queueSave(game);lastSave=now;}
  double drawStart=perf?clockSeconds():0;Gfx::begin();ui.draw(game);Gfx::end();++rendered;
  if(frames){std::fprintf(frames,"%d,%llu,%u,%llu,%d,%d\n",rendered,(unsigned long long)game.ticks,Gfx::presentVcount(),Gfx::presentTime(),Gfx::misses(),Gfx::cachedBytes());if(rendered%120==0)std::fflush(frames);}
  if(uiTrace&&(changed||pressed||now-lastUi>=.25)){uiProbe(uiTrace,ui,game,rendered,pressed,pad.Buttons);lastUi=now;}
  if(perf&&now-lastPerf>=1){auto mem=mallinfo();peakHeap=std::max(peakHeap,size_t(mem.uordblks));std::fprintf(perf,"%.3f,%llu,%.3f,%d,%d,%d,%d,%u,%d,%u,%u,%u,%d,%d,%u,%u,%u\n",now-lastPerf,(unsigned long long)game.ticks,(clockSeconds()-drawStart)*1000,Gfx::cachedBytes(),Audio::residentBytes(),mem.uordblks,mem.fordblks,sceKernelTotalFreeMemSize(),Gfx::misses(),Gfx::diskReads(),Gfx::diskBytes(),Gfx::prefetches(),scePowerGetCpuClockFrequencyInt(),scePowerGetBusClockFrequencyInt(),Gfx::pinnedBytes(),Gfx::submissions(),Gfx::listPeakBytes());std::fflush(perf);lastPerf=now;}
  (void)nextDraw;
  if(qa&&replayTick>=replay.total){
   if(frames)std::fflush(frames);if(perf)std::fflush(perf);
   if(FILE* done=std::fopen("QA_DONE.txt","w")){std::fprintf(done,"source_ticks=%d presentations=%d\n",replayTick,rendered);std::fclose(done);}
   break;
  }
 }
 waitForSave();if(ui.page==UI::Battle&&!qa)game.save("SUSPEND.DAT");saveRunning=false;if(saveThread>=0){sceKernelSignalSema(saveWake,1);sceKernelWaitThreadEnd(saveThread,nullptr);sceKernelDeleteThread(saveThread);}if(saveWake>=0)sceKernelDeleteSema(saveWake);if(trace)std::fclose(trace);if(perf){std::fprintf(perf,"# peak_heap=%u rendered=%d\n",unsigned(peakHeap),rendered);std::fclose(perf);}
 if(frames)std::fclose(frames);if(uiTrace)std::fclose(uiTrace);Audio::stop();Gfx::shutdown();sceKernelExitGame();return 0;
}
