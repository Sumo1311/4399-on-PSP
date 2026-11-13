#pragma once

#include "fighter_asset.hpp"
#include "stage_motion.hpp"

#include <cmath>
#include <algorithm>

namespace bvn {

struct WorldBox {
    double left = 0, top = 0, right = 0, bottom = 0;

    bool empty() const { return left >= right || top >= bottom; }
    bool overlaps(WorldBox other) const {
        return left < other.right && other.left < right && top < other.bottom && other.top < bottom;
    }
    WorldBox intersection(WorldBox other) const {
        if (!overlaps(other)) return {};
        return {std::max(left,other.left),std::max(top,other.top),
                std::min(right,other.right),std::min(bottom,other.bottom)};
    }
    Point center() const { return {(left+right)*.5,(top+bottom)*.5}; }
};

inline WorldBox worldBox(FighterBox box, Point anchor, int facing) {
    return {anchor.x + (facing > 0 ? box.left : -box.right), anchor.y + box.top,
            anchor.x + (facing > 0 ? box.right : -box.left), anchor.y + box.bottom};
}

inline bool inStrikeRange(Point first, Point second) {
    return std::abs(first.x - second.x) <= 600 && std::abs(first.y - second.y) <= 600;
}

inline OpticalSprite opticalSprite(WorldBox body,bool destroyed=false){
    return {{body.left,body.top,body.right-body.left,body.bottom-body.top},true,destroyed};
}

} // namespace bvn
