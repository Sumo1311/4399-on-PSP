#include "game.hpp"
namespace aow {
// Literal translation of Computer/AI.as; AI.xml remains the event authority.
void Game::loadAI() { aiEvent=0; nextAIEvent(); }
void Game::nextAIEvent() {
 const auto& events=aiDefs[age[1]-1];
 if(aiEvent>=int(events.size())) { if(age[1]<7) { evolve(2); loadAI(); } return; }
 const auto& e=events[aiEvent++];
 aiUnits.clear();
 for(auto& a:e.sell) if(std::string(a.kind)=="turret") sellTurret(2,a.spot);
 // E4X loops are grouped by tag name, not by mixed XML document order.
 for(auto& a:e.build) if(std::string(a.kind)=="tower") addTower(2);
 for(auto& a:e.build) if(std::string(a.kind)=="turret") createTurret(a.id,2,a.spot);
 for(auto& a:e.build) if(std::string(a.kind)=="special") createSpecial(2,age[1]);
 aiTime=e.duration; aiMax=e.maximum; aiMin=e.minimum;
 aiUnitTime=random()*(aiMax-aiMin)+aiMin;
 aiUnits=e.units;
}
void Game::updateAI(double dt) {
 aiTime-=dt; aiUnitTime-=dt;
 if(aiTime<=0) nextAIEvent();
 if(aiUnitTime<=0) {
  double draw=random(),sum=0;
  for(auto& c:aiUnits) {
   if(draw<c.ratio+sum) {
    if(unitDefs[c.id-1].pop+pop[1]<=10) {
     pop[1]+=int(unitDefs[c.id-1].pop);createUnit(c.id,2);
    }
    break;
   }
   sum+=c.ratio;
  }
  aiUnitTime=random()*(aiMax-aiMin)+aiMin;
 }
}
}
