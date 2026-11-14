#pragma once
#include "game.hpp"
#include <vector>
namespace aow {
struct Action {std::string key;int kind,id;float x,y,w,h;};
class UI {
public:
 enum Page {Menu,Difficulty,Battle,Credits,Guide,Links};
 Page page=Menu,returnPage=Menu;int selection=0,focus=0,pauseSelection=0;bool details=false,wantExit=false,returnPaused=false;
 std::string focusKey;int focusKind=-1,focusId=0;
 void normalize(const Game&);
 float music=1,sound=1;double toastTime=0;std::string toast;
 std::vector<Action> actions(const Game&) const;
 void move(Game&,int dx,int dy);void confirm(Game&);void back(Game&);void triangle(Game&);void start(Game&);void square(Game&);void help(Game&);
 void draw(const Game&);void update(double dt);void notify(const std::string&);
};
}
