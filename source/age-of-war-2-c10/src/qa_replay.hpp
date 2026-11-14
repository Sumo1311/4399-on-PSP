#pragma once
#include "game.hpp"
#include <string>
namespace aow {
struct InputEvent {int tick;std::string action;int id;};
struct Replay {
 std::string caseId;uint32_t seed=1;double difficulty=1;int total=0,every=15;std::vector<InputEvent> events;
 bool read(const std::string&);void apply(Game&,int) const;
};
std::string sampleRow(const Game&,int tick);
}
