#include "fighter_asset.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
int main(int argc,char**argv){
 if(argc!=3)return 1;std::ifstream names(argv[2]);std::string name;
 while(std::getline(names,name)){
  bvn::FighterAsset a;if(!a.open((std::filesystem::path(argv[1])/name).c_str(),bvn::FighterAsset::TextureResidency::LowMemory))return 2;
  std::cout<<"BANK\t"<<name<<'\t'<<a.metadataIdentity()<<'\n';
  std::map<unsigned,std::pair<unsigned,unsigned>> poses;
  for(unsigned c=0;c<a.clipCount();++c)for(unsigned f=0;f<a.clip(bvn::MoveClip(c)).count;++f)for(const auto&p:a.poseTiles(a.frame(bvn::MoveClip(c),f).pose)){
   const auto dim=std::pair<unsigned,unsigned>{p.stride,p.rows};
   if(poses.contains(p.offset)&&poses[p.offset]!=dim)return 3;poses[p.offset]=dim;
  }
  for(auto [id,dim]:poses)std::cout<<"TEX\t"<<name<<'\t'<<id<<'\t'<<dim.first<<'\t'<<dim.second<<'\n';
 }
}
