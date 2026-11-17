#pragma once
#include <string>
struct JSContext;
int de_storage_bind(JSContext* context);
bool de_storage_read_payload(const char* name,std::string& payload);
void de_storage_use_coop(const std::string& settings,const std::string& progress);
