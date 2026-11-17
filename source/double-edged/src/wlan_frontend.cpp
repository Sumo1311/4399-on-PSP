#include "wlan_frontend.hpp"
#include "source_vm.hpp"
#include "storage.hpp"
#include "render_psp.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <string>
#include <vector>
#include <pspkernel.h>
#include <zlib.h>
extern "C" {__attribute__((used,externally_visible)) volatile doubleedged::WlanProbe g_double_edged_link{};}
extern "C" void double_edged_link_trace(const char* message){if(FILE* f=std::fopen("LINK.LOG","a")){std::fprintf(f,"%llu %s\n",sceKernelGetSystemTimeWide(),message);std::fclose(f);}}
namespace doubleedged {
namespace {
bool exists(const char* name){FILE* f=std::fopen(name,"rb");if(!f)return false;std::fclose(f);return true;}
bool file_crc(const std::string& name,std::uint32_t& result){FILE* f=std::fopen(name.c_str(),"rb");if(!f)return false;unsigned char block[4096];size_t size,total=0;uLong crc=result;while((size=std::fread(block,1,sizeof(block),f))){crc=crc32(crc,block,size);total+=size;}bool valid=total&&!std::ferror(f);std::fclose(f);result=crc;return valid;}
bool source_crc(std::uint32_t& result){
    std::vector<std::string> names={"runtime.js","classes.js","visual_policy.js","movie.json","pad.js","link_input.js","link_clock.js","link_profile.js","graphics.swf","art.pak","fonts.pak","audio.pak"};
    DIR* directory=opendir("levels");if(!directory)return false;std::vector<std::string> levels;while(auto file=readdir(directory)){std::string name=file->d_name;if(name.size()>4&&name.substr(name.size()-4)==".xml")levels.push_back("levels/"+name);}closedir(directory);std::sort(levels.begin(),levels.end());if(levels.size()!=12)return false;names.insert(names.end(),levels.begin(),levels.end());
    result=0;for(const auto& name:names){result=crc32(result,reinterpret_cast<const Bytef*>(name.c_str()),name.size()+1);if(!file_crc(name,result))return false;}return true;
}
JSValue method(JSContext* c,const char* name,JSValue& object){JSValue global=JS_GetGlobalObject(c);object=JS_GetPropertyStr(c,global,"DEProfile");JS_FreeValue(c,global);return JS_GetPropertyStr(c,object,name);}
bool encode(SourceVM& vm,WlanSetup& setup){
    if(!vm.eval_file("link_profile.js"))return false;std::string settings="{}",progress="{}",controls="{}";de_storage_read_payload("so_doubleedged",settings);de_storage_read_payload("coop_so_doubleedged1",progress);de_storage_read_payload("so_doubleedged1",controls);
    auto c=vm.context();JSValue object,fn=method(c,"encode",object),args[]={JS_NewStringLen(c,settings.data(),settings.size()),JS_NewStringLen(c,progress.data(),progress.size()),JS_NewStringLen(c,controls.data(),controls.size())};JSValue wire=JS_Call(c,fn,object,3,args);for(auto a:args)JS_FreeValue(c,a);JS_FreeValue(c,fn);JS_FreeValue(c,object);
    if(JS_IsException(wire)){vm.capture_exception();JS_FreeValue(c,wire);return false;}
    bool valid=true;for(unsigned i=0;i<setup.preferences.size();i++){JSValue value=JS_GetPropertyUint32(c,wire,i);uint32_t n=0;if(JS_ToUint32(c,&n,value)||n>255)valid=false;JS_FreeValue(c,value);setup.preferences[i]=n;}JS_FreeValue(c,wire);return valid;
}
}
const char* WlanFrontend::status_text(WlanStatus s){switch(s){case WlanStatus::Searching:return "Searching for room...";case WlanStatus::Waiting:return "Waiting for second PSP...";case WlanStatus::Connecting:return "Connecting...";case WlanStatus::Active:return "Connected";case WlanStatus::WlanOff:return "Turn on the WLAN switch";case WlanStatus::Timeout:return "Connection timed out";case WlanStatus::PeerLeft:return "Other PSP disconnected";case WlanStatus::Incompatible:return "Different game version";case WlanStatus::Desynchronized:return "Link out of sync";case WlanStatus::Failed:return "Connection failed";default:return "Choose a role";}}
bool WlanFrontend::connect(SourceVM& vm,bool qa,volatile bool& exiting){
    qa_=qa;WlanSetup setup;setup.seed=unsigned(sceKernelGetSystemTimeWide())&0x7fffffffU;if(!setup.seed)setup.seed=42;
    if(!source_crc(setup.source_crc)||!file_crc("EBOOT.PBP",setup.executable_crc)||!encode(vm,setup)){double_edged_link_trace("Cannot prepare room compatibility/profile data");return false;}
    bool host=true;unsigned last=0;
    if(qa_){FILE* f=std::fopen("WLAN.QA","r");unsigned role=0,seed=0;bool valid=f&&std::fscanf(f,"%u %u",&role,&seed)==2&&role<=1&&seed&&seed<=0x7fffffffU;if(f)std::fclose(f);if(!valid)return false;host=role!=0;setup.seed=seed;session.start(host,setup,sceKernelGetSystemTimeWide());
        if(!exists("NOINPUT.QA")){if(FILE* input=std::fopen("WLAN.INPUT","rb")){std::fseek(input,0,SEEK_END);long size=std::ftell(input);std::rewind(input);if(size<0||size>3000000||size%3){std::fclose(input);return false;}scripted_.resize(size);bool read=std::fread(scripted_.data(),1,size,input)==size;std::fclose(input);if(!read)return false;}}
    }
    while(!exiting){
        SceCtrlData pad{};sceCtrlPeekBufferPositive(&pad,1);unsigned edge=pad.Buttons&~last;last=pad.Buttons;
        if(edge&PSP_CTRL_CIRCLE)return false;
        if(!qa_&&!session.busy()){if(edge&(PSP_CTRL_UP|PSP_CTRL_DOWN))host=!host;if(edge&PSP_CTRL_CROSS)session.start(host,setup,sceKernelGetSystemTimeWide());}
        session.pump(sceKernelGetSystemTimeWide());probe();if(session.active()){set_psp_link_role(host?1:2);return true;}
        char text[420];std::snprintf(text,sizeof(text),"DOUBLE EDGED - TWO PSP\n\n%s CREATE ROOM (P1)\n%s JOIN ROOM (P2)\n\n%s\n\nUP/DOWN: CHOOSE  CROSS: CONNECT\nCIRCLE: EXIT\n\nP1 CHOOSES MENUS. START PAUSES.",host?">":" ",host?" ":">",status_text(session.status()));draw_psp_message(text);sceKernelDelayThreadCB(1000);
    }return false;
}
bool WlanFrontend::apply_profile(SourceVM& vm){
    auto c=vm.context();JSValue object,fn=method(c,"decode",object),wire=JS_NewArray(c);for(unsigned i=0;i<session.setup().preferences.size();i++)JS_SetPropertyUint32(c,wire,i,JS_NewUint32(c,session.setup().preferences[i]));JSValue result=JS_Call(c,fn,object,1,&wire);JS_FreeValue(c,wire);JS_FreeValue(c,fn);JS_FreeValue(c,object);
    if(JS_IsException(result)){vm.capture_exception();JS_FreeValue(c,result);return false;}
    JSValue settings=JS_GetPropertyStr(c,result,"settings"),progress=JS_GetPropertyStr(c,result,"progress");const char* a=JS_ToCString(c,settings);const char* b=JS_ToCString(c,progress);bool valid=a&&b;if(valid)de_storage_use_coop(a,b);JS_FreeCString(c,a);JS_FreeCString(c,b);JS_FreeValue(c,settings);JS_FreeValue(c,progress);JS_FreeValue(c,result);return valid;
}
void WlanFrontend::sample(const SceCtrlData& pad){
    pending_buttons_|=pad.Buttons&~last_buttons_;last_buttons_=pad.Buttons;if(!session.wants_input())return;
    WlanInput input;unsigned buttons=pad.Buttons|pending_buttons_;constexpr unsigned masks[]={PSP_CTRL_UP,PSP_CTRL_DOWN,PSP_CTRL_LEFT,PSP_CTRL_RIGHT,PSP_CTRL_SQUARE,PSP_CTRL_CROSS,PSP_CTRL_CIRCLE,PSP_CTRL_START};for(unsigned i=0;i<8;i++)if(buttons&masks[i])input.keys|=1U<<i;input.analog_x=pad.Lx;input.analog_y=pad.Ly;
    if(!scripted_.empty()){size_t at=size_t(session.next_input_frame())*3;input={};if(at+3<=scripted_.size()){input.keys=scripted_[at];input.analog_x=scripted_[at+1];input.analog_y=scripted_[at+2];}}
    if(session.submit(input))pending_buttons_=0;
}
bool WlanFrontend::checksum(SourceVM& vm){
    auto c=vm.context();JSValue global=JS_GetGlobalObject(c),fn=JS_GetPropertyStr(c,global,"pspLinkSignature"),value=JS_Call(c,fn,JS_UNDEFINED,0,nullptr);JS_FreeValue(c,fn);JS_FreeValue(c,global);if(JS_IsException(value)){vm.capture_exception();JS_FreeValue(c,value);return false;}
    size_t size=0;const char* text=JS_ToCStringLen(c,&size,value);if(!text){JS_FreeValue(c,value);return false;}unsigned hash=crc32(0,reinterpret_cast<const Bytef*>(text),size);JS_FreeCString(c,text);JS_FreeValue(c,value);session.checksum(session.frame(),hash);probe(hash);return session.active();
}
void WlanFrontend::probe(unsigned hash){unsigned frame=session.frame();for(unsigned i=0;i<8&&i<=frame;i++)if(session.confirmed(frame-i)){confirmed_=std::max(confirmed_,frame-i);break;}g_double_edged_link.status=unsigned(session.status());g_double_edged_link.role=session.host()?1:2;g_double_edged_link.frame=frame;g_double_edged_link.confirmed=confirmed_;g_double_edged_link.checksum=hash;g_double_edged_link.error=session.error();}
}
