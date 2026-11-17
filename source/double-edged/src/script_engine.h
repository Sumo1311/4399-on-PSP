#pragma once
#include "duktape.h"
#include <stddef.h>
#include <stdint.h>
duk_context *de_script_create(void);
int de_script_eval_file(duk_context *ctx,const char *path);
int de_script_json_file(duk_context *ctx,const char *path,const char *global_name);
int de_script_eval(duk_context *ctx,const char *source);
size_t de_script_allocated(void);
size_t de_script_peak(void);
size_t de_script_weak_live(void);
uint64_t de_clock_us(void);
void de_script_destroy(duk_context *ctx);
