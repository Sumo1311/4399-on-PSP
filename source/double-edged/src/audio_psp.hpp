#pragma once
struct JSContext;
#ifdef __cplusplus
extern "C" {
#endif
// Source sound IDs address the original event records in audio.pak.
int de_audio_bind(JSContext* context);
void de_audio_begin_frame(unsigned frame);
void de_audio_pause(bool paused);
void de_audio_flush();
unsigned de_audio_query_count();
void de_audio_shutdown();
#ifdef __cplusplus
}
#endif
