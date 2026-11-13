// Adapted from GameCamera and GameStage.initCamera.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "stage_motion.hpp"
#include <array>
#include <cmath>
#include <span>
#include <limits>
namespace bvn {
struct SourceCameraRect {double x=0,y=0,width=800,height=600;};
class SourceCamera {
public:
    void initialize(double width,double bottom,double playerBottom,unsigned hz){
        *this={};tween=int(2.5/(30./hz));offsetY=bottom-playerBottom;
        setStageBounds({0,-1000,width,600});setZoom(2);setX(-(width-350));setY(200-bottom);
        updateNow({});
    }
    void setStageBounds(SourceCameraRect value){bounds_=value;setZoom(zoom_);}
    void setZoom(double value){
        zoom_=value;const double inv=1/zoom_;
        target_.width=800*inv;target_.height=600*inv;
        focusOffset_={800*.5*inv,600*.5*inv};
        border_={bounds_.x*zoom_,bounds_.y*zoom_,bounds_.width-800*inv,bounds_.height-600*inv};
    }
    void focus(unsigned count){hasFocus_=true;hasPoint_=count>1;focusCount_=count;point_={};}
    void move(Point point){hasFocus_=false;hasPoint_=true;point_=point;}
    void setX(double value){
        if(value<border_.x)value=border_.x;
        if(value>border_.width)value=border_.width;
        target_.x=value;
        if(tween>1)display_.x+=(value-display_.x)/tween;
        else display_.x=value;
    }
    void setY(double value){
        if(value<border_.y)value=border_.y;
        if(value>border_.height)value=border_.height;
        target_.y=value;
        if(tween>1)display_.y+=(value-display_.y)/tween;
        else display_.y=value;
    }
    bool updateNow(std::span<const Point> points){
        const int old=tween;tween=0;
        if(!render(points))return false;
        tween=old;return true;
    }
    bool render(std::span<const Point> points){
        if(!hasFocus_&&!hasPoint_)return true;
        // Original render accesses _focus.length even after move() set it null.
        if(!hasFocus_)return false;
        if(points.size()<focusCount_)return false;
        if(focusCount_>1){
            const auto a=points.front(),b=points[focusCount_-1];double dx=0,dy=0;
            if(focusX){const double left=a.x<b.x?a.x:b.x,right=a.x<b.x?b.x:a.x;dx=right-left;point_.x=left+dx*.5;}
            if(focusY){const double top=a.y<b.y?a.y:b.y,bottom=a.y<b.y?b.y:a.y;dy=bottom-top;point_.y=top+dy*.5;}
            if(autoZoom){
                double x=zoom_,y=zoom_;
                if(focusX)x=800/dx*.8;
                if(focusY)y=600/dy*.8;
                double value=std::isnan(x)||std::isnan(y)?std::numeric_limits<double>::quiet_NaN():std::fmin(x,y);
                if(value<zoomMin)value=zoomMin;
                if(value>zoomMax)value=zoomMax;
                setZoom(value);
            }
        }
        if((focusX||focusY)&&!hasPoint_&&focusCount_==0)return false;
        if(focusX)setX((hasPoint_?point_.x:points[0].x)-(focusOffset_.x+offsetX));
        if(focusY)setY((hasPoint_?point_.y:points[0].y)-(focusOffset_.y+offsetY));
        if(scale_!=zoom_){
            if(zoom_<=0)return false;
            if(tween>1)scale_+=(zoom_-scale_)/tween;
            else scale_=zoom_;
            const double inv=1/scale_;
            display_.width=asInt(800*inv+1);display_.height=asInt(600*inv+1);
        }
        if(!applied_||appliedRect_.x!=display_.x||appliedRect_.y!=display_.y||appliedRect_.width!=display_.width||appliedRect_.height!=display_.height){
            appliedRect_=display_;++scrollWrites_;
        }
        if(!applied_||appliedScale_!=scale_){appliedScale_=scale_;scaleWrites_+=2;}
        applied_=true;return true;
    }
    const SourceCameraRect& target()const{return target_;}
    const SourceCameraRect& display()const{return display_;}
    double zoom()const{return zoom_;}double scale()const{return scale_;}
    unsigned focusCount()const{return hasFocus_?focusCount_:0;}
    std::array<double,21> snapshot()const{return {target_.x,target_.y,target_.width,target_.height,
        display_.x,display_.y,display_.width,display_.height,zoom_,scale_,double(tween),point_.x,point_.y,
        double(scrollWrites_),double(scaleWrites_),applied_?appliedRect_.x:0,applied_?appliedRect_.y:0,
        applied_?appliedRect_.width:0,applied_?appliedRect_.height:0,applied_?appliedScale_:0,double(applied_)};}
    int tween=0;
    bool focusX=true,focusY=true,autoZoom=true;
    double offsetX=0,offsetY=0,zoomMin=1,zoomMax=2.5;
private:
    static int asInt(double value){
        if(!std::isfinite(value)||value==0)return 0;
        double low=std::fmod(std::trunc(value),4294967296.);if(low<0)low+=4294967296.;
        return low<2147483648.?int(low):int(low-4294967296.);
    }
    SourceCameraRect target_{},display_{},bounds_{},border_{},appliedRect_{};
    Point focusOffset_{},point_{};
    double zoom_=1,scale_=1,appliedScale_=1;
    unsigned focusCount_=0,scrollWrites_=0,scaleWrites_=0;
    bool hasFocus_=false,hasPoint_=false,applied_=false;
};
}
