#pragma once
#include <string>
namespace aow {class Audio {public:static bool init();static void stop();static void music(const std::string&);static void effect(const std::string&,float volume);static void volumes(float music,float effects);static int residentBytes();};}
