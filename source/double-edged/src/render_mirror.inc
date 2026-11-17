// Source-thread-owned primitive mirror. It retains no JSValue or game object.
// Only dirty fields cross the VM boundary; playback still uses immutable Frames.
struct FastWorld {
    // Translations stay in integral source twips. Render arithmetic uses the
    // PSP float unit; source/physics still uses unchanged flash_math.h.
    float m[6]={1,0,0,1,0,0},previous[6]={1,0,0,1,0,0},c[8]={256,256,256,256,0,0,0,0};
    bool previous_matches_prior=true,current_matches_prior=true,current_finite=true;unsigned owner=0;
};
struct MirrorNode {
    int symbol=-1,kind=-1;
    FastWorld world;
    float matrix[6]={1,0,0,1,0,0},color[8]={256,256,256,256,0,0,0,0},old_matrix[6]={1,0,0,1,0,0};
    unsigned history_frame=~0u;
    std::vector<unsigned> children;
    std::vector<MirrorNode*> child_nodes;unsigned child_nodes_epoch=0;
    std::string text;
    unsigned uid=0,ratio=0,depth=0,clip=0,matrix_version=0,color_version=0,used=0,parent_uid=0;
    bool visible=true,container=false,html=false,corpse=false,death_opaque=false;
};
std::unordered_map<unsigned,MirrorNode> mirror_nodes;
unsigned mirror_pointer_epoch=1;
bool mirror_enabled=false,mirror_check=false;
std::vector<unsigned> corpse_nodes;
unsigned long long corpse_actor_samples=0,corpse_hidden_samples=0,corpse_dying_samples=0,corpse_guard_fallbacks=0,corpse_draw_skips=0;
bool corpse_hidden(unsigned uid){auto found=mirror_nodes.find(uid);return found!=mirror_nodes.end()&&found->second.corpse;}
bool corpse_kept_opaque(unsigned uid){auto found=mirror_nodes.find(uid);return found!=mirror_nodes.end()&&found->second.death_opaque;}
void update_corpse_visibility(JSContext* ctx,JSValueConst root){
    for(auto uid:corpse_nodes){auto found=mirror_nodes.find(uid);if(found!=mirror_nodes.end()){found->second.corpse=false;found->second.death_opaque=false;}}
    corpse_nodes.clear();
    auto stats=devisual::collect_corpses(ctx,root,property_atoms[Game],property_atoms[Baddies],property_atoms[Health],property_atoms[Alive],property_atoms[Landed],property_atoms[DeadCount],property_atoms[Shadow],property_atoms[Uid],[&](unsigned uid,devisual::DeathVisual phase){
        auto found=mirror_nodes.find(uid);if(found!=mirror_nodes.end()){found->second.corpse=phase==devisual::DeathVisual::Corpse;found->second.death_opaque=phase==devisual::DeathVisual::Dying;corpse_nodes.push_back(uid);}
    });
    corpse_actor_samples+=stats.actors;corpse_hidden_samples+=stats.dead;corpse_dying_samples+=stats.dying;corpse_guard_fallbacks+=stats.unsupported;
}
JSValue mirror_update(JSContext* ctx,JSValueConst,int argc,JSValueConst* argv){
    if(argc!=2)return JS_ThrowTypeError(ctx,"Render update arguments");
    try {
        auto node=argv[0];unsigned uid=unsigned(number(ctx,node,Uid));uint32_t flags=0;JS_ToUint32(ctx,&flags,argv[1]);
        auto found=mirror_nodes.find(uid);if(found==mirror_nodes.end())flags=63;
        auto& out=mirror_nodes[uid];out.uid=uid;
        if(flags==63){out.symbol=number(ctx,node,Symbol);auto def=prop(ctx,node,Definition);out.kind=JS_IsObject(def)?number(ctx,def,Kind):-1;}
        if(flags&1){auto a=prop(ctx,node,Matrix);double values[6];if(!JS_ReadNumericArray(a,values,6)){for(unsigned i=0;i<6;i++){Value v(ctx,JS_GetPropertyUint32(ctx,a,i));if(JS_ToFloat64(ctx,&values[i],v))return JS_EXCEPTION;}}for(unsigned i=0;i<6;i++)out.matrix[i]=float(i>=4?values[i]*20:values[i]);out.matrix_version=unsigned(number(ctx,node,MatrixVersion));}
        if(flags&2){auto a=prop(ctx,node,Color);double values[8];if(!JS_ReadNumericArray(a,values,8)){for(unsigned i=0;i<8;i++){Value v(ctx,JS_GetPropertyUint32(ctx,a,i));if(JS_ToFloat64(ctx,&values[i],v))return JS_EXCEPTION;}}for(unsigned i=0;i<8;i++)out.color[i]=float(values[i]);out.color_version=unsigned(number(ctx,node,ColorVersion));}
        if(flags&4){out.child_nodes.clear();out.child_nodes_epoch=0;auto a=prop(ctx,node,Children);out.container=JS_IsObject(a);out.children.clear();if(out.container){unsigned n=length(ctx,a);out.children.reserve(n);for(unsigned i=0;i<n;i++){Value c(ctx,JS_GetPropertyUint32(ctx,a,i));out.children.push_back(unsigned(number(ctx,c,Uid)));}}}
        if(flags&8){out.ratio=unsigned(number(ctx,node,Ratio));out.depth=unsigned(number(ctx,node,TimelineDepth));out.clip=unsigned(number(ctx,node,ClipDepth));}
        if((flags&16)&&out.kind==37){auto h=prop(ctx,node,Html);out.html=!JS_IsNull(h)&&!JS_IsUndefined(h);auto t=prop(ctx,node,out.html?Html:Text);out.text=string_value(ctx,t);}
        if(flags&32){auto v=prop(ctx,node,Visible);out.visible=!(JS_IsBool(v)&&!JS_ToBool(ctx,v));}
        return JS_UNDEFINED;
    }catch(const std::exception& e){return JS_ThrowInternalError(ctx,"Render mirror: %s",e.what());}
}
void fast_matrix(const float* a,const float* b,float* out){
    out[0]=a[0]*b[0]+a[2]*b[1];out[1]=a[1]*b[0]+a[3]*b[1];
    out[2]=a[0]*b[2]+a[2]*b[3];out[3]=a[1]*b[2]+a[3]*b[3];
    out[4]=float(de_round_twips(a[0]*b[4]+a[2]*b[5]))+std::floor(a[4]+.5f);
    out[5]=float(de_round_twips(a[1]*b[4]+a[3]*b[5]))+std::floor(a[5]+.5f);
}
bool same_finite_matrix(const float* a,const float* b){for(unsigned i=0;i<6;i++)if(std::memcmp(a+i,b+i,sizeof(float))||!std::isfinite(a[i]))return false;return true;}
const FastWorld& mirror_world(MirrorNode& node,const FastWorld& parent,bool allow_death_alpha){
    auto m=node.matrix;auto& w=node.world;
    bool prior=node.history_frame!=~0u&&node.history_frame+1==drawing_frame;
    // world.m was the previous frame's composed matrix, not the local matrix.
    // Keep a separate local history so moving parents interpolate correctly.
    float previous[6];std::copy_n(prior?node.old_matrix:m,6,previous);
    if(std::fabs(m[4]-previous[4])>4000||std::fabs(m[5]-previous[5])>4000)std::copy_n(m,6,previous);
    float determinant=m[0]*m[3]-m[1]*m[2],old_determinant=previous[0]*previous[3]-previous[1]*previous[2];
    float alignment=0;for(unsigned i=0;i<4;++i)alignment+=m[i]*previous[i];
    if(determinant*old_determinant<=0||alignment<=0)std::copy_n(m,4,previous);
    // Reparenting changes the composition even when both parents are static.
    bool same_parent=prior&&node.parent_uid==parent.owner;
    // Consecutive,unchanged operands produce exactly the prior composition.
    // The interpolation discontinuity guards above remain authoritative.
    if(same_parent&&w.current_finite&&parent.previous_matches_prior&&same_finite_matrix(previous,node.old_matrix))std::copy_n(w.m,6,w.previous);
    else fast_matrix(parent.previous,previous,w.previous);
    w.previous_matches_prior=prior&&same_finite_matrix(w.previous,w.m);
    if(same_parent&&w.current_finite&&parent.current_matches_prior&&same_finite_matrix(m,node.old_matrix))w.current_matches_prior=true;
    else{float current[6];fast_matrix(parent.m,m,current);w.current_matches_prior=prior&&same_finite_matrix(current,w.m);w.current_finite=true;for(unsigned i=0;i<6;i++)if(!std::isfinite(current[i]))w.current_finite=false;std::copy_n(current,6,w.m);}
    node.parent_uid=parent.owner;w.owner=node.uid;
    for(unsigned i=0;i<4;i++){w.c[i]=parent.c[i]*node.color[i]/256.f;w.c[i+4]=parent.c[i]*node.color[i+4]/256.f+parent.c[i+4];}
    if(allow_death_alpha&&node.death_opaque){w.c[3]=parent.c[3];w.c[7]=parent.c[7];}
    std::copy_n(m,6,node.old_matrix);node.history_frame=drawing_frame;
    return w;
}
void display_fast_leaf(const gc_ptr<character>& object,unsigned ratio,const FastWorld& world){
    matrix m,previous;
    m.m_[0][0]=world.m[0];m.m_[1][0]=world.m[1];m.m_[0][1]=world.m[2];m.m_[1][1]=world.m[3];m.m_[0][2]=world.m[4];m.m_[1][2]=world.m[5];
    previous.m_[0][0]=world.previous[0];previous.m_[1][0]=world.previous[1];previous.m_[0][1]=world.previous[2];previous.m_[1][1]=world.previous[3];previous.m_[0][2]=world.previous[4];previous.m_[1][2]=world.previous[5];
    cxform c;for(unsigned i=0;i<4;i++){c.m_[i][0]=world.c[i]/256.f;c.m_[i][1]=world.c[i+4];}
    if(capture_recorder)capture_recorder->set_motion(m,previous);
    object->set_matrix(m);object->set_cxform(c);object->set_ratio(float(ratio)/65535);object->display();++native_leaves;
}
static unsigned long long direct_baked_tries=0,direct_baked_hits=0;
bool record_baked_leaf(unsigned symbol,int kind,const FastWorld& world,character* parent){
#if DE_DIRECT_BAKED_CAPTURE
    // Static shape display first asks the renderer for this exact baked ID at
    // ratio0. Keep text,buttons,morphs and absent art on the original path.
    if(capture_recorder&&(kind==2||kind==22||kind==32||kind==83)){
        ++direct_baked_tries;matrix local,previous;
        local.m_[0][0]=world.m[0];local.m_[1][0]=world.m[1];local.m_[0][1]=world.m[2];local.m_[1][1]=world.m[3];local.m_[0][2]=world.m[4];local.m_[1][2]=world.m[5];
        previous.m_[0][0]=world.previous[0];previous.m_[1][0]=world.previous[1];previous.m_[0][1]=world.previous[2];previous.m_[1][1]=world.previous[3];previous.m_[0][2]=world.previous[4];previous.m_[1][2]=world.previous[5];
        matrix absolute=local;
        for(auto ancestor=parent;ancestor;ancestor=ancestor->get_parent()){matrix next=ancestor->get_matrix();next.concatenate(absolute);absolute=next;}
        cxform tint,absolute_tint;for(unsigned i=0;i<4;i++){tint.m_[i][0]=world.c[i]/256.f;tint.m_[i][1]=world.c[i+4];}
        if(parent)absolute_tint=parent->get_world_cxform();absolute_tint.concatenate(tint);
        capture_recorder->set_motion(local,previous);
        if(capture_recorder->draw_shape(int(symbol),absolute,absolute_tint,0)){++direct_baked_hits;++native_leaves;return true;}
    }
#endif
    return false;
}
void mark_mirror_tree(unsigned uid){
    auto it=mirror_nodes.find(uid);if(it==mirror_nodes.end())return;
    auto& node=it->second;node.used=drawing_frame;for(auto child:node.children)mark_mirror_tree(child);
}
void mirror_tree_node(MirrorNode& node,const FastWorld& parent_world,bool as_mask,movie_definition_sub* definitions,character* parent){
    unsigned uid=node.uid;node.used=drawing_frame;++native_nodes;
    if(!as_mask&&(!node.visible||node.corpse)){if(node.corpse)++corpse_draw_skips;return;}
    bool button=node.kind==34;
    if(node.container&&node.children.empty()&&!button)return;
    const auto& world=mirror_world(node,parent_world,!as_mask);if(world.c[3]<=0&&!as_mask)return;
    if(node.container&&!button){
        if(node.child_nodes_epoch!=mirror_pointer_epoch){
            node.child_nodes.clear();node.child_nodes.reserve(node.children.size());
            for(auto child:node.children){auto found=mirror_nodes.find(child);if(found==mirror_nodes.end())throw std::runtime_error("Missing primitive render child "+std::to_string(child));node.child_nodes.push_back(&found->second);}
            node.child_nodes_epoch=mirror_pointer_epoch;
        }
        std::vector<unsigned> ends;
        for(auto child:node.child_nodes){
            auto& c=*child;
            while(!ends.empty()&&c.depth>ends.back()){paint_renderer()->disable_mask();ends.pop_back();}
            if(c.clip){paint_renderer()->begin_submit_mask();mirror_tree_node(*child,world,true,definitions,parent);paint_renderer()->end_submit_mask();ends.push_back(c.clip);}
            else mirror_tree_node(*child,world,as_mask,definitions,parent);
        }
        while(!ends.empty()){paint_renderer()->disable_mask();ends.pop_back();}
    }else if(node.symbol>=0){
        unsigned id=unsigned(node.symbol);if(record_baked_leaf(id,node.kind,world,parent))return;gc_ptr<character> object;
        if(node.kind==37){auto& item=text_cache[uid];if(!item.object){auto d=definitions->get_character_def(id);if(!d)throw std::runtime_error("Missing mirrored text definition");item.object=d->create_character_instance(parent,id);}
            if(!item.initialized||node.text!=item.text||node.html!=item.html){item.object->set_member(node.html?"htmlText":"text",as_value(node.text.c_str()));item.text=node.text;item.html=node.html;item.initialized=true;}item.used=drawing_frame;object=item.object;
        }else {auto& cached=shape_cache[id];if(!cached){auto d=definitions->get_character_def(id);if(!d)throw std::runtime_error("Missing mirrored drawing definition");cached=d->create_character_instance(parent,id);}object=cached;}
        display_fast_leaf(object,node.ratio,world);
    }
}
void mirror_tree(unsigned uid,const FastWorld& world,bool as_mask,movie_definition_sub* definitions,character* parent){
    auto found=mirror_nodes.find(uid);if(found==mirror_nodes.end())throw std::runtime_error("Missing primitive render node "+std::to_string(uid));
    mirror_tree_node(found->second,world,as_mask,definitions,parent);
}
bool near_matrix(const matrix& a,const matrix& b){for(unsigned y=0;y<2;y++)for(unsigned x=0;x<3;x++)if(std::fabs(a.m_[y][x]-b.m_[y][x])>(x==2?.02f:.00001f))return false;return true;}
bool near_color(const cxform& a,const cxform& b){for(unsigned y=0;y<4;y++)for(unsigned x=0;x<2;x++)if(std::fabs(a.m_[y][x]-b.m_[y][x])>(x?.002f:.00001f))return false;return true;}
bool same_commands(const psp60::Frame& a,const psp60::Frame& b){
    if(a.commands.size()!=b.commands.size()||a.coordinates!=b.coordinates)return false;
    for(unsigned i=0;i<a.commands.size();++i){auto& p=a.commands[i];auto& q=b.commands[i];if(p.op!=q.op)return false;
        using psp60::Op;
        switch(p.op){
        case Op::Shape:if(p.a!=q.a||p.b!=q.b||!near_color(p.cx,q.cx))return false;[[fallthrough]];
        case Op::Matrix:if(!near_matrix(p.m,q.m)||!near_matrix(p.previous,q.previous))return false;break;
        case Op::Color:if(!near_color(p.cx,q.cx))return false;break;
        case Op::FillBitmap:if(p.a!=q.a||p.b!=q.b||p.c!=q.c||p.bitmap!=q.bitmap||!near_matrix(p.m,q.m))return false;break;
        case Op::Bitmap:if(p.bitmap!=q.bitmap||std::memcmp(&p.bounds,&q.bounds,sizeof(p.bounds))||std::memcmp(&p.uv,&q.uv,sizeof(p.uv))||std::memcmp(&p.color,&q.color,sizeof(p.color))||!near_matrix(p.m,q.m)||!near_matrix(p.previous,q.previous))return false;break;
        case Op::Strip:case Op::Triangles:case Op::Line:case Op::FillOff:if(p.a!=q.a||p.offset!=q.offset)return false;break;
        case Op::FillColor:if(p.a!=q.a)return false;[[fallthrough]];
        case Op::LineColor:if(std::memcmp(&p.color,&q.color,sizeof(p.color)))return false;break;
        case Op::LineWidth:if(p.width!=q.width)return false;break;
        default:break;
        }
    }
    return true;
}
