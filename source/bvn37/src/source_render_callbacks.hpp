// Copyright (C) 2021-2026, 5DPLAY Game Studio
// Adapted from GameRender's same-owner Vector iteration.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <vector>
namespace bvn {
class SourceRenderCallbacks {
public:
    enum class Group : unsigned { Before, Main, After };
    using Id=unsigned;
    void add(Group group,Id id){auto& list=lists_[unsigned(group)];if(std::find(list.begin(),list.end(),id)==list.end())list.push_back(id);}
    void remove(Group group,Id id){auto& list=lists_[unsigned(group)];if(auto i=std::find(list.begin(),list.end(),id);i!=list.end())list.erase(i);}
    bool contains(Group group,Id id)const{const auto& list=lists_[unsigned(group)];return std::find(list.begin(),list.end(),id)!=list.end();}
    const std::vector<Id>& list(Group group)const{return lists_[unsigned(group)];}
    template<class Call> void render(Call&& call){
        for(auto& list:lists_){
            const auto length=list.size();
            for(std::size_t i=0;i<length;++i){
                if(i>=list.size())break;
                const auto id=list[i];call(id);
            }
        }
    }
private:std::array<std::vector<Id>,3> lists_{};
};
}
