#include "fighter_asset.hpp"
#include <fstream>
#include <iostream>
#include <map>
#include <filesystem>
int main(int argc,char**argv){if(argc!=3)return 1;std::ifstream names(argv[2]);std::string n;unsigned banks=0,textures=0,hd=0;unsigned long long bytes=0;
while(std::getline(names,n)){bvn::FighterAsset a;if(!a.open((std::filesystem::path(argv[1])/n).c_str(),bvn::FighterAsset::TextureResidency::LowMemory)){std::cerr<<"open "<<n;return 2;}if(!a.hasObjectAreas()){std::cerr<<"profile "<<n;return 3;}std::map<unsigned,bvn::FighterPose> poses;for(unsigned c=0;c<a.clipCount();++c)for(unsigned f=0;f<a.clip(bvn::MoveClip(c)).count;++f)for(const auto&p:a.poseTiles(a.frame(bvn::MoveClip(c),f).pose))poses[p.offset]=p;
for(auto [id,p]:poses){std::vector<std::byte> raw(a.pixelBytes(p));if(!a.decodePixels(p,raw)){std::cerr<<"decode "<<n<<' '<<id;return 4;}++textures;hd+=a.textureScale(p)==2;bytes+=raw.size();}++banks;}
std::cout<<"PASS banks="<<banks<<" textures="<<textures<<" native2x="<<hd<<" decoded_bytes="<<bytes<<'\n';}
