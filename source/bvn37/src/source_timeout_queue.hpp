// Copyright (C) 2021-2026, 5DPLAY Game Studio
// Adapted from pinned LIB_KyoLib KyoTimeout.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cmath>
#include <optional>
#include <span>
#include <vector>
namespace bvn {
class SourceTimeoutQueue {
public:
    struct Entry{unsigned id;int frames;std::vector<int> params;};
    void frameTimeout(unsigned id,int frames,std::vector<int> params={}){entries_.push_back(Entry{id,frames,std::move(params)});listening_=true;}
    void timeout(unsigned id,int milliseconds,unsigned stageHz,std::vector<int> params={}){frameTimeout(id,int(std::ceil((double(milliseconds)/1000)*stageHz)),std::move(params));}
    template<class Call> void render(Call&& call){
        auto n=entries_.size();if(!n){listening_=false;return;}
        for(std::size_t i=0;i<n;++i){
            if(!entries_[i]){entries_.erase(entries_.begin()+i);i=0;n=entries_.size();continue;}
            if(entries_[i]->frames--<=0){
                const auto value=*entries_[i];call(value.id,std::span<const int>(value.params));entries_[i].reset();
            }
        }
    }
    bool listening()const{return listening_;}
    const std::vector<std::optional<Entry>>& entries()const{return entries_;}
private:std::vector<std::optional<Entry>> entries_{};bool listening_=false;
};
}
