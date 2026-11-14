// Compare real source-profile super completion across the roster and maps.
#include "fight_match.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
int main(int argc,char**argv) {
    if(argc!=4)return 2;
    auto root=std::filesystem::path(argv[1]);std::ifstream names(argv[2]);std::string name;
    std::map<std::string,std::vector<std::string>> groups;
    std::ifstream groupFile(argv[3]);std::string line;
    while(std::getline(groupFile,line)) {
        std::istringstream row(line);std::vector<std::string> files;std::string f;
        while(row>>f)files.push_back(f);
        for(const auto& bank:files)groups[bank]=files;
    }
    bvn::FighterAsset rival;
    if(!rival.open((root/"NARUTO.BVF").c_str(),bvn::FighterAsset::TextureResidency::LowMemory))return 3;
    while(std::getline(names,name)) {
        const auto& group=groups.at(name);std::vector<bvn::FighterAsset> forms(group.size());unsigned initial=0;
        for(unsigned i=0;i<group.size();++i) {
            if(!forms[i].open((root/group[i]).c_str(),bvn::FighterAsset::TextureResidency::LowMemory))return 4;
            if(group[i]==name)initial=i;
        }
        const auto& player=forms[initial];
        if(!player.has(player.command(bvn::FighterCommand::Super)))continue;
        for(const char* map:{"ZOULANG.BVN","SENLIN.BVN"}) {
            bvn::StageAsset stage;if(!stage.open((root/map).c_str()))return 5;
            const auto geometry=stage.geometry();
            for(unsigned hz:{30u,60u})for(unsigned side:{0u,1u}) {
                bvn::FightMatch game(forms,std::span(&rival,1),{initial,0});
                const double x=(geometry.left+geometry.right)*.5;
                game.reset(stage,{x+(side?120:-120),geometry.playerBottom},{x+(side?-120:120),geometry.playerBottom});
                game.setSourceProfile(hz,30,stage);
                game.actor(0).setQi(300);game.actor(0).startTraining();game.actor(1).startTraining();
                unsigned frame=0;bool started=false,complete=false;
                for(unsigned tick=0;tick<1200;++tick) {
                    bvn::FighterControls controls{};if(tick==2)controls.superPressed=true;
                    game.step(geometry,{{controls,{}}},true);
                    const auto& actor=game.actor(0);frame=actor.timelineFrame();
                    started|=actor.acting()||game.cinematic();
                    if(started&&frame<10&&!actor.acting()&&!game.cinematic()){complete=true;break;}
                    if(game.actor(0).motion().position.x!=game.actor(0).motion().position.x)return 6;
                }
                std::cout<<"{\"bank\":\""<<name<<"\",\"map\":\""<<map<<"\",\"hz\":"<<hz<<",\"side\":"<<side
                    <<",\"started\":"<<started<<",\"completed\":"<<complete<<",\"last_frame\":"<<frame<<"}\n";
            }
        }
    }
}
