#include "ui.hpp"
#include "hud_layout.hpp"
#include "gfx.hpp"
#include "audio.hpp"
#include "platform_data.hpp"
#include "source_parts.hpp"
#include "qa_replay.hpp"
#include <algorithm>
#include <cstdio>
#include <psprtc.h>
namespace aow { namespace {
constexpr float z=272.f/480;
float px(double x){return 36+x*z;} float py(double y){return y*z+20;}
uint32_t alpha(double a){return (uint32_t(std::max(0.,std::min(1.,a))*255)<<24)|0xffffff;}
constexpr uint32_t ink=0xffd9e7ee,gold=0xff68c6f5,muted=0xff9caea9,ground=0xff222824;
void skin(float x,float y,float w,float h){
 const float edge=4;float xx[]={x,x+edge,x+w-edge},yy[]={y,y+edge,y+h-edge};float ww[]={edge,w-edge*2,edge},hh[]={edge,h-edge*2,edge};
 Gfx::rect(x,y,w,h,ground);
 for(int iy=0;iy<3;iy++)for(int ix=0;ix<3;ix++){std::string key="skin/panel"+std::to_string(iy)+std::to_string(ix);Gfx::sprite(key,0,xx[ix],yy[iy],ww[ix]/(ix==1?52:6),hh[iy]/(iy==1?52:6));}
}
void panel(float x,float y,float w,float h){skin(x,y,w,h);}
void focusFrame(hud::Rect r){Gfx::border(r.x,r.y,r.w,r.h,gold);Gfx::border(r.x+1,r.y+1,r.w-2,r.h-2,gold);}
void button(hud::Rect r,const std::string& s,bool selected){panel(r.x,r.y,r.w,r.h);if(selected){Gfx::rect(r.x+3,r.y+3,r.w-6,r.h-6,0x304fa4e8);focusFrame(r);}Gfx::text(s,r.x+9,r.y+std::floor((r.h-18)/2),selected?gold:ink,1.3333f,r.w-18);}
void padIcon(int kind,float x,float y){Gfx::sprite("skin/pad"+std::to_string(kind),0,x,y);}

void world(const Game& g){
 auto sprite=[&](const std::string& k,int f,double x,double y,double sx=1,double sy=1,double r=0,uint32_t c=0xffffffff){Gfx::sprite(k,f,px(x+g.camera),py(y+390),sx,sy,r,c);};
 sprite("backgrounds",std::max(g.age[0],g.age[1])-1,0,0);
 for(int side=0;side<2;++side){double bx=side?490.7:-491.3,flip=side?-1:1;sprite("bases_fla.Base_2",g.age[side]-1,bx,100,flip,1);const double ty[]={-243.75,-290.2,-336.6,-383.0};const char* names[]={"bases_fla.Tower01_3","bases_fla.Tower02_4","bases_fla.Tower03_5","bases_fla.Tower04_6"};for(int i=0;i<g.spots[side];++i)sprite(names[i],g.age[side]-1,bx-130.5*flip,100+ty[i],.75*flip,.75);}
 for(const auto& u:g.units){const auto& d=unitDefs[u.id-1];sprite("u"+std::to_string(u.id),u.animation.frame,u.x,u.y,d.scale*(u.side==1?-1:1),d.scale);if(u.hp>0&&(u.unitMelee||u.unitRange||g.showDamage)){float x=px(u.x+g.camera)-8,y=py(390-u.h-7);Gfx::rect(x,y,17,3,0xff000000);Gfx::rect(x+1,y+1,15,1,0xff0000bb);Gfx::rect(x+1,y+1,15*float(u.hp/u.maxHP),1,0xff00ff00);}}
 for(const auto& t:g.turrets)if(t.alive){const auto& d=turretDefs[t.id-1];const auto& p=turretParts[t.id-1];sprite("t"+std::to_string(t.id),0,t.x,t.y,d.scale*(t.side==1?1:-1),d.scale);sprite(p.key,t.animation.frame,t.x+p.x*d.scale,t.y+p.y*d.scale,d.scale*p.sx,d.scale*p.sy*(t.side==1?1:-1),t.rotation);}
 for(const auto& p:g.projectiles)sprite(p.name,0,p.x,p.y,p.scale*(p.side==1?-1:1),p.scale*(p.side==1?-1:1),p.rotation);
 auto decorative=[](const Effect& e){return e.kind=="damage"||e.kind=="explosion"||e.kind=="firetrail";};
 auto visible=[&](const Effect& e){float x=px(e.x+g.camera),y=py(e.y+390);return x>=36&&x<444&&y>=74&&y<249;};
 size_t first=g.effects.size();int count=0;while(first>0){--first;if(decorative(g.effects[first])&&visible(g.effects[first])&&++count==2)break;}
 for(size_t i=0;i<g.effects.size();++i){const auto& e=g.effects[i];if(decorative(e)&&(i<first||!visible(e)))continue;std::string key;int f=0;double sx=e.scale,sy=e.scale;
 if(e.kind=="special6"){key="special6_plane";sx=e.side==1?1:-1;}else if(e.kind=="meteor")key="special1";else if(e.kind=="lightning"){key="special2";f=e.count;}else if(e.kind=="fireball"){key="special3";f=e.animation.frame;}else if(e.kind=="arrow")key="special4";else if(e.kind=="canonball")key="special5";else if(e.kind=="planebomb"){key="special6_bomb";sx=e.side==1?1:-1;}else if(e.kind=="laserbeam"){key="special7";f=e.count;}else if(e.kind=="explosion"||e.kind=="firetrail"){key=e.kind;f=e.animation.frame;}else if(e.kind=="damage")Gfx::text(std::to_string(int(e.value)),px(e.x+g.camera),py(e.y+390),alpha(e.alpha),.7);
 if(!key.empty())sprite(key,f,e.x,e.y,sx,sy,e.rotation,alpha(e.alpha));}
}
struct Info{std::string name,desc,cost;};
Info info(const Action& a,const Game& g){Info v;double cost=0;bool xp=false;
 if(a.kind==0){auto& d=unitDefs[a.id-1];v.name=d.name;v.desc=unitDescriptions[a.id-1];cost=d.cost;}
 else if(a.kind==1){auto& d=turretDefs[a.id-1];v.name=d.name;v.desc=turretDescriptions[a.id-1];cost=d.cost;}
 else if(a.kind==2){auto& d=upgradeDefs[a.id-1];int lv=std::max(0,std::min(g.upgrades[0][a.id-1],int(d.levels.size())-1));if(!d.levels.empty()){v.name=d.levels[lv].name;v.desc=d.levels[lv].desc;cost=d.levels[lv].cost;}}
 else if(a.kind==3){auto& d=specialDefs[a.id-1];v.name=d.name;v.desc=d.desc;cost=d.cost;xp=true;}
 else if(a.kind==4){if(g.age[0]>=7){v.name="最高时代";v.desc="已完成全部进化";return v;}auto& d=evolutions[std::min(int(evolutions.size())-1,g.age[0]-1)];v.name=d.name;v.desc=d.desc;cost=d.cost;xp=true;}
 else if(a.kind==5){auto& d=towers[std::min(3,g.spots[0])];v.name=d.name;v.desc=d.desc;cost=d.cost;}
 else {v.name=a.kind==6?"出售炮塔":a.kind==7?"取消":a.kind==8?"放置炮塔":"出售此炮塔";return v;}
 v.cost=std::to_string(int(cost))+(xp?" 经验":" 金币");return v;}
}
std::vector<Action> UI::actions(const Game& g)const{std::vector<Action> a;a.reserve(32);int age=g.age[0];auto add=[&](std::string k,int kind,int id){a.push_back({k,kind,id,0,0,32,32});};int begin=age==7?24:age*4-3,end=age==7?29:age*4;for(int i=begin;i<=end;++i)add("btnU"+std::to_string(i),0,i);const int tb[]={1,4,5,7,10,12,15},te[]={3,4,6,9,11,14,17};if(!g.pendingTurret&&!g.selling)for(int i=tb[age-1];i<=te[age-1];++i)add("btnT"+std::to_string(i),1,i);add("btnS"+std::to_string(age),3,age);add("btnEvolve",4,0);if(!g.pendingTurret&&!g.selling){add("btnAddTower",5,0);add("btnSellTurret",6,0);}else add("btnCancel",7,0);int n=a.size();for(int i=0;i<n;++i){auto r=hud::primary(i,n);a[i].x=r.x;a[i].y=r.y;}
 for(int i=0;i<12;++i){int id=i?i+7:age;auto r=hud::upgrade(i);a.push_back({"btnUP"+std::to_string(id),2,id,r.x,r.y,r.w,r.h});}
 if(g.pendingTurret||g.selling)for(int spot=1;spot<=g.spots[0];++spot){bool taken=false;for(auto& t:g.turrets)if(t.alive&&t.side==1&&t.spot==spot)taken=true;if((g.pendingTurret&&!taken)||(g.selling&&taken)){float x=std::round(px(-624+g.camera)-10),y=std::round(std::max(74.f,std::min(229.f,py(390-146-47*(spot-1))-10)));if(x>=36&&x+20<=444)a.push_back({"spot",g.pendingTurret?8:9,spot,x,y,20,20});}}return a;}
void UI::normalize(const Game& g){auto a=actions(g);if(a.empty()){focus=0;focusKey.clear();focusKind=-1;return;}int found=-1;for(int i=0;i<int(a.size());++i)if(a[i].key==focusKey&&a[i].kind==focusKind&&a[i].id==focusId){found=i;break;}if(found<0){if(focusKey.empty())found=std::max(0,std::min(focus,int(a.size())-1));else{for(int i=0;i<int(a.size());++i)if(a[i].kind==7){found=i;break;}if(found<0)found=0;}details=false;}focus=found;focusKey=a[focus].key;focusKind=a[focus].kind;focusId=a[focus].id;}
void UI::notify(const std::string& s){toast=s;toastTime=2.5;}void UI::update(double dt){toastTime=std::max(0.,toastTime-dt);}
void UI::move(Game& g,int dx,int dy){if(page!=Battle){int n=page==Menu?5:page==Difficulty?4:1;selection=(selection+(dy?dy:dx)+n)%n;return;}if(g.tutorial||g.result)return;if(g.paused){pauseSelection=(pauseSelection+(dy?dy:dx)+6)%6;return;}normalize(g);auto a=actions(g);focus=hud::navigate(a,focus,dx,dy);focusKey=a[focus].key;focusKind=a[focus].kind;focusId=a[focus].id;}
void UI::confirm(Game& g){if(page==Menu){if(selection==0){page=Difficulty;selection=1;}else if(selection==1)page=Credits;else if(selection==2)help(g);else page=Links;return;}if(page==Difficulty){if(selection==3){notify("MAXGAMES 专属");return;}double values[]={.6,1,1.5};uint64_t tick;sceRtcGetCurrentTick(&tick);g.reset(values[selection],uint32_t(tick));page=Battle;focus=0;focusKey.clear();normalize(g);details=false;Audio::music("Music1");return;}if(page!=Battle){back(g);return;}if(g.result){page=Menu;selection=0;Audio::music("Music2");return;}if(g.tutorial){g.tutorial=g.paused=false;return;}if(g.paused){switch(pauseSelection){case 0:g.paused=false;break;case 1:if(g.save("SUSPEND.DAT")){notify("已存档");page=Menu;Audio::music("Music2");}else notify("存档失败");break;case 2:sound=sound?0:1;Audio::volumes(music,sound);break;case 3:music=music?0:1;Audio::volumes(music,sound);break;case 4:page=Menu;Audio::music("Music2");break;case 5:wantExit=true;}return;}
 // A disappearing focus must be reviewed, never confirmed as a different item.
 std::string old=focusKey;int oldKind=focusKind,oldId=focusId;normalize(g);if(!old.empty()&&(old!=focusKey||oldKind!=focusKind||oldId!=focusId))return;auto a=actions(g);if(a.empty())return;Action e=a[focus];switch(e.kind){case 0:g.queueUnit(e.id);break;case 1:g.buyTurret(e.id);break;case 2:g.buyUpgrade(e.id);break;case 3:g.buySpecial(e.id);break;case 4:g.buyEvolve();break;case 5:g.addTower();break;case 6:{bool any=false;for(auto& t:g.turrets)if(t.alive&&t.side==1)any=true;if(any)g.selling=true;else g.error="你没有炮塔可出售";break;}case 7:g.cancelTurret();break;case 8:g.placeTurret(e.id);break;case 9:g.sellTurret(1,e.id);break;}if(!g.error.empty()){notify(g.error);g.error.clear();}if((e.kind==1||e.kind==6)&&(g.pendingTurret||g.selling)){g.camera=800;auto list=actions(g);for(auto& v:list)if(v.kind==8||v.kind==9){focusKey=v.key;focusKind=v.kind;focusId=v.id;break;}}normalize(g);}
void UI::back(Game& g){if(page==Battle){if(g.pendingTurret||g.selling){g.cancelTurret();normalize(g);}else if(details)details=false;else start(g);}else{if(page==Guide){page=returnPage;g.paused=returnPaused;}else page=Menu;selection=0;}}
void UI::help(Game& g){if(page==Guide)return;returnPage=page;returnPaused=g.paused;if(page==Battle)g.paused=true;page=Guide;selection=0;}
void UI::triangle(Game& g){if(page==Battle)details=!details;else help(g);}void UI::start(Game& g){if(page==Battle&&!g.tutorial&&!g.result){g.paused=!g.paused;pauseSelection=0;}}
void UI::square(Game& g){if(page==Menu){if(g.load("SUSPEND.DAT")){FILE* flag=std::fopen("QA_COLD.txt","r");if(flag){std::fclose(flag);g.save("QA_RESTORED.DAT");FILE* row=std::fopen("QA_RESTORED.tsv","wb");if(row){auto state=sampleRow(g,int(g.ticks));std::fprintf(row,"%s\n",state.c_str());std::fclose(row);}}page=Battle;g.paused=false;g.tutorial=false;focusKey.clear();normalize(g);Audio::music("Music1");notify("继续游戏");}else notify("没有可读取的存档");}else if(page==Battle&&!g.paused&&!g.result)g.speed=g.speed==1?2:1;}
void UI::draw(const Game& g){Gfx::clip(false);if(page==Battle){normalize(g);Gfx::clip(true);world(g);Gfx::clip(false);Gfx::rect(0,0,480,74,ground);Gfx::sprite("skin/panel11",0,0,0,480.f/52,74.f/52);Gfx::rect(0,73,480,1,0xff48667e);Gfx::rect(0,249,480,23,ground);Gfx::rect(0,249,480,1,0xff48667e);Gfx::text("升级",6,55,muted,1);auto a=actions(g);
 for(int kind:{0,1,3,4,5}){for(auto& e:a)if(e.kind==kind){Gfx::text(kind==0?"单位":kind==1?"炮塔":kind==3?"技能":kind==4?"进化":"建造",e.x,0,muted,.8);break;}}
 for(int i=0;i<int(a.size());++i){auto& e=a[i];uint32_t color=e.kind==0?0xffa4aa73:e.kind==1?0xffaf9d79:e.kind==2?0xffb78c9f:gold;Gfx::rect(e.x,e.y,e.w,e.h,ground);if(e.kind<8)Gfx::sprite(e.kind==2?"skin/button22":"skin/button32",0,e.x,e.y);else panel(e.x,e.y,e.w,e.h);Gfx::rect(e.x+4,e.y+e.h-2,e.w-8,1,color);if(e.kind>=8)Gfx::text(e.kind==8?"+":"-",e.x+6,e.y+5);else{std::string key="hud/"+e.key;if(Gfx::asset(key))Gfx::sprite(key,0,e.x+2,e.y+2);else Gfx::text(e.kind==0?"兵":e.kind==1?"塔":e.kind==2?"升":e.kind==3?"技":e.kind==4?"进":e.kind==5?"建":e.kind==6?"售":"退",e.x+4,e.y+5,0xffffffff,.85,e.w-4);}
 bool locked=false;if(e.kind==0){const auto* need=unitDefs[e.id-1].needUpgrade;for(int k=0;k<int(upgradeDefs.size());++k)if(std::string(need)==upgradeDefs[k].name&&g.upgrades[0][k]==0)locked=true;}bool unavailable=(e.kind==4&&g.age[0]==7)||(e.kind==5&&g.spots[0]>=4);if(e.kind==6){unavailable=true;for(auto& t:g.turrets)if(t.alive&&t.side==1)unavailable=false;}if(locked||unavailable){Gfx::rect(e.x+e.w-8,e.y+2,6,7,0xff202020);Gfx::text(locked?"L":"-",e.x+e.w-8,e.y+2,0xffaaaaee,.6);}
 if(e.kind==0){int n=std::count(g.queue.begin(),g.queue.end(),e.id);if(n)Gfx::text(std::to_string(n),e.x+2,e.y+21,0xffffffff,.8);if(g.building==e.id&&g.buildMax>0)Gfx::rect(e.x+1,e.y+e.h-3,(e.w-2)*float(std::max(0.,std::min(1.,g.buildTime/g.buildMax))),2,0xff00ff00);}if(e.kind==3&&g.specialTimer>0)Gfx::rect(e.x+1,e.y+e.h-3,(e.w-2)*float(std::min(1.,g.specialTimer/60)),2,0xffcccccc);
 if(i==focus&&!g.paused&&!g.result){focusFrame({e.x,e.y,e.w,e.h});}}
 if(!a.empty()){auto v=info(a[focus],g);Gfx::rect(358,49,1,23,0xff48667e);Gfx::text(v.name,365,45,gold,1,113);Gfx::text(v.cost,365,60,ink,1,113);if(details&&!g.paused&&!g.result){panel(80,143,320,98);Gfx::text(v.name+"  "+v.cost,87,150,0xff00ffff,.9,306);Gfx::text(v.desc,87,169,0xffffffff,.9,306);}}
 for(int side=0;side<2;++side){float x=side?242:2;Gfx::rect(x,250,236,9,0xff151515);float ratio=g.maxHP[side]>0?float(g.hp[side])/g.maxHP[side]:0;Gfx::rect(x,250,236*std::max(0.f,std::min(1.f,ratio)),9,side?0xff6554a3:0xff98a857);Gfx::text(std::to_string(g.hp[side])+"/"+std::to_string(g.maxHP[side]),x+4,248,0xffffffff,.8);}
 Gfx::text("x"+std::to_string(g.speed),4,260,ink,.8);Gfx::text("人口 "+std::to_string(g.pop[0])+"/10",40,260,0xffffffff,.8);Gfx::text("经验 "+std::to_string(g.xp),160,260,0xffd4c38a,.8);Gfx::text("金币 "+std::to_string(g.money),320,260,gold,.8);
 panel(0,74,36,175);panel(444,74,36,175);padIcon(0,13,84);Gfx::text("确认",8,97,ink,1);padIcon(1,13,120);Gfx::text("返回",8,133,ink,1);padIcon(2,13,156);Gfx::text("资料",8,169,ink,1);Gfx::text("L/R",451,84,gold,1);Gfx::text("视角",452,97,ink,1);padIcon(3,457,125);Gfx::text("速度",452,140,ink,1);Gfx::text("START",445,176,gold,.8);Gfx::text("暂停",452,189,ink,1);
 if(g.paused&&!g.tutorial){Gfx::rect(36,74,408,175,0xbb000000);panel(145,51,190,171);const char* labels[]={"继续游戏","保存并返回","音效","音乐","返回主菜单","退出游戏"};for(int i=0;i<6;++i){std::string s=labels[i];if(i==2)s+=sound?" 开":" 关";if(i==3)s+=music?" 开":" 关";button(hud::pause(i),s,i==pauseSelection);}}
 if(g.tutorial){panel(56,80,368,161);Gfx::text("操作说明\n方向键选择，X 确认，L/R 移动视角\n三角资料，圆圈取消，SELECT 帮助\n方块切换速度，START 暂停\n消灭敌军获得金币和经验。\n建造炮塔，进化并摧毁敌方基地。",66,88,0xffffffff,1,348);button(hud::returning(),"X 开始",true);}
 if(g.result){Gfx::sprite(g.result==1?"victory":"defeat",0,36,0);button(hud::returning(),"返回主菜单",true);}
 }else if(page==Menu||page==Difficulty){Gfx::sprite(page==Menu?"menu":"difficulty",0,36,0);panel(186,page==Menu?70:108,240,page==Menu?181:140);const char* m[]={"开始游戏","制作名单","操作帮助","原作网站","攻略链接"};const char* d[]={"简单 0.6","普通 1.0","困难 1.5","极难 2.0（原站限定）"};int n=page==Menu?5:4;for(int i=0;i<n;++i)button(hud::menu(i,n),page==Menu?m[i]:d[i],selection==i);if(page==Menu)Gfx::text("方块：读取存档",12,255,0xffffffff,1);
 }else if(page==Credits){Gfx::sprite("credits",0,36,0);panel(150,201,180,59);button(hud::returning(),"返回",true);
 }else{Gfx::sprite("menu",0,36,0);panel(40,16,400,245);if(page==Links)Gfx::text("原作链接\n\nwww.maxgames.com\nwww.interactive-pixels.net\nwww.u77.com\nwww.maxgames.com/guides/age-of-war-2.html",50,27,0xffffffff,1,380);else Gfx::text("操作说明\n\n方向键 选择按钮    X 确认\nL / R 移动视角    三角 查看资料\n圆圈 返回或取消   SELECT 帮助\n方块 游戏速度     START 暂停\n\n消灭敌军获得金币和经验。\n使用经验进化或发动特殊攻击。\n建造炮塔，保护基地并摧毁敌方基地。\n主菜单按方块读取已保存的战局。",50,25,0xffffffff,1,380);button(hud::returning(),"返回",true);}
 if(toastTime>0){panel(48,214,384,30);Gfx::text(toast,56,222,gold,1,368);}Gfx::clip(false);
}
}
