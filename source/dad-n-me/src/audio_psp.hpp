#pragma once
#include "gameswf/gameswf.h"
gameswf::sound_handler* make_psp_audio();
void pause_psp_audio(gameswf::sound_handler*,bool);
