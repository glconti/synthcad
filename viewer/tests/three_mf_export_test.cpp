#include "three_mf_export.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <sstream>
#include <stdexcept>

using namespace dingcad;
namespace {
void Require(bool value,const char *message) { if(!value) throw std::runtime_error(message); }
std::string Read(const std::filesystem::path &path) { std::ifstream in(path,std::ios::binary); return {std::istreambuf_iterator<char>(in),{}}; }
unsigned U16(const std::string &s,size_t offset) { return static_cast<unsigned char>(s.at(offset)) | (unsigned(static_cast<unsigned char>(s.at(offset+1)))<<8); }
uint32_t U32(const std::string &s,size_t offset) { return U16(s,offset) | (uint32_t(U16(s,offset+2))<<16); }
uint32_t Crc(const std::string &s) {
  uint32_t value=0xffffffff;
  for(unsigned char c:s) { value^=c; for(int i=0;i<8;++i) value=(value&1)?(value>>1)^0xedb88320u:value>>1; }
  return ~value;
}
std::map<std::string,std::string> Unzip(const std::filesystem::path &path) {
  const auto bytes=Read(path); std::map<std::string,std::string> result; size_t cursor=0;
  while(U32(bytes,cursor)==0x04034b50) {
    Require(U16(bytes,cursor+8)==0,"ZIP stored compression required");
    const auto length=U32(bytes,cursor+18), nameLength=U16(bytes,cursor+26), extraLength=U16(bytes,cursor+28);
    const auto name=bytes.substr(cursor+30,nameLength), data=bytes.substr(cursor+30+nameLength+extraLength,length);
    Require(data.size()==length && Crc(data)==U32(bytes,cursor+14),"ZIP CRC/size mismatch");
    result.emplace(name,data); cursor+=30+nameLength+extraLength+length;
  }
  const size_t directory=cursor;
  for(size_t i=0;i<result.size();++i) {
    Require(U32(bytes,cursor)==0x02014b50,"Missing central directory entry");
    const auto local=U32(bytes,cursor+42);
    Require(U32(bytes,local)==0x04034b50 && U32(bytes,cursor+16)==U32(bytes,local+14),"Central directory references/CRC mismatch");
    cursor+=46+U16(bytes,cursor+28)+U16(bytes,cursor+30)+U16(bytes,cursor+32);
  }
  Require(U32(bytes,cursor)==0x06054b50 && U16(bytes,cursor+10)==result.size() && U32(bytes,cursor+16)==directory && cursor+22==bytes.size(),"Invalid end of central directory");
  return result;
}
size_t Count(const std::string &s,const std::string &needle) { size_t n=0,p=0; while((p=s.find(needle,p))!=std::string::npos){++n;p+=needle.size();} return n; }
DisplayPart Instance(const std::string &id,const std::shared_ptr<manifold::Manifold> &source) {
  DisplayPart p; p.id=id; p.name="città 日本 & <\"box\">"; p.sourcePartId="shared&source"; p.sourceSolid=source;
  p.rotation={31,47,83}; p.translation={40,-5,12};
  p.solid=std::make_shared<manifold::Manifold>(source->Rotate(31,47,83).Translate({40,-5,12}));
  p.memberships={{{"g1","Group one"}},{{"g2","Alias"}}}; return p;
}
void CheckTransform(const std::string &xml,const manifold::Manifold &placed) {
  std::smatch match;
  Require(std::regex_search(xml,match,std::regex("transform=\"([^\"]+)\"")),"Missing instance transform");
  double m[12]; std::istringstream matrix(match[1]); for(double &v:m) Require(bool(matrix>>v),"Malformed transform");
  double lo[3]={1e30,1e30,1e30},hi[3]={-1e30,-1e30,-1e30};
  std::vector<std::array<double,3>> transformed;
  const std::regex vertex("<vertex x=\"([^\"]+)\" y=\"([^\"]+)\" z=\"([^\"]+)\"/>");
  for(std::sregex_iterator it(xml.begin(),xml.end(),vertex),end;it!=end;++it) {
    double p[3]={std::stod((*it)[1]),std::stod((*it)[2]),std::stod((*it)[3])};
    std::array<double,3> point;
    for(int a=0;a<3;++a) {const double v=m[a]*p[0]+m[a+3]*p[1]+m[a+6]*p[2]+m[a+9];point[a]=v;lo[a]=std::min(lo[a],v);hi[a]=std::max(hi[a],v);}
    transformed.push_back(point);
  }
  const auto bounds=placed.BoundingBox();
  for(int a=0;a<3;++a) Require(std::abs(lo[a]-bounds.min[a])<1e-5 && std::abs(hi[a]-bounds.max[a])<1e-5,"3MF transform differs from placed Manifold XYZ rotation");
  const auto mesh=placed.GetMeshGL64();
  for(const auto &point:transformed) {
    bool found=false;
    for(size_t v=0;v<mesh.NumVert();++v) {
      bool match=true;
      for(int a=0;a<3;++a) match=match && std::abs(point[a]-mesh.vertProperties[v*mesh.numProp+a])<1e-9;
      found=found||match;
    }
    Require(found,"Transformed source vertex differs from placed Manifold vertex");
  }
}
}
int main(int argc,char **argv) { try {
  const auto root=std::filesystem::temp_directory_path()/("dingcad-3mf-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  const auto path=root/"nested"/"scene.3mf"; std::string error;
  auto source=std::make_shared<manifold::Manifold>(manifold::Manifold::Cube({2,3,5}).CalculateNormals(0,30));
  auto first=Instance("first",source),second=Instance("second",source),reference=Instance("reference",source);
  reference.exportable=false; reference.name="NEVER_EXPORTED";
  PartTree tree; tree.Reload({first,second,reference});
  Require(ExportParts3mf(tree,false,false,path,true,error)==ExportResult::Invalid && !std::filesystem::exists(path),"Invalid load must not write");
  Require(ExportParts3mf(tree,false,true,path,false,error)==ExportResult::Saved && error.empty(),"Valid export failed");
  auto archive=Unzip(path); Require(archive.size()==3,"Core archive has exactly three OPC parts");
  const auto xml=archive.at("3D/3dmodel.model");
  Require(archive.at("_rels/.rels").find("Target=\"/3D/3dmodel.model\"")!=std::string::npos && archive.at("[Content_Types].xml").find("3dmanufacturing-3dmodel+xml")!=std::string::npos,"OPC model relationship/content type missing");
  Require(xml.find("unit=\"millimeter\"")!=std::string::npos && xml.find("città 日本 &amp; &lt;&quot;box&quot;&gt;")!=std::string::npos,"Units or escaped UTF-8 names missing");
  Require(Count(xml,"<mesh>")==1 && Count(xml,"<component ")==2 && Count(xml,"<item ")==2,"Shared sources/alias memberships/coincident distinct instances changed");
  Require(Count(xml,"<vertex ")==8,"Property seam vertices must be topologically welded for core 3MF");
  std::map<std::pair<unsigned,unsigned>,unsigned> edges;
  const std::regex triangles("<triangle v1=\"([0-9]+)\" v2=\"([0-9]+)\" v3=\"([0-9]+)\"/>");
  for(std::sregex_iterator it(xml.begin(),xml.end(),triangles),end;it!=end;++it) {
    unsigned v[3]={unsigned(std::stoul((*it)[1])),unsigned(std::stoul((*it)[2])),unsigned(std::stoul((*it)[3]))};
    for(int i=0;i<3;++i) ++edges[{v[i],v[(i+1)%3]}];
  }
  for(const auto &edge:edges) Require(edge.second==1 && edges.at({edge.first.second,edge.first.first})==1,"Core 3MF mesh must retain closed oriented connectivity");
  Require(xml.find("NEVER_EXPORTED")==std::string::npos && xml.find("Bambu")==std::string::npos,"Reference or vendor settings leaked");
  Require(xml.find("<metadatagroup><metadata name=\"dingcad:instanceId\" preserve=\"true\">first</metadata>")!=std::string::npos && xml.find("shared&amp;source")!=std::string::npos,"Authored identity missing");
  CheckTransform(xml,*first.solid);
  if(argc>1) {
    // The externally imported fixture is already a sensible current plate:
    // grounded, separated and within a typical bed. Arbitrary XYZ transforms
    // remain covered above without inviting slicer auto-placement behavior.
    auto groundedA=first,groundedB=second;
    groundedA.rotation={0,0,37}; groundedA.translation={40,40,0}; groundedA.name="Plate città A";
    groundedB.rotation={0,0,0}; groundedB.translation={90,40,0}; groundedB.name="Plate 日本 B";
    groundedA.solid=std::make_shared<manifold::Manifold>(source->Rotate(0,0,37).Translate({40,40,0}));
    groundedB.solid=std::make_shared<manifold::Manifold>(source->Translate({90,40,0}));
    PartTree plate; plate.Reload({groundedA,groundedB});
    Require(ExportParts3mf(plate,false,true,std::filesystem::u8path(argv[1]),true,error)==ExportResult::Saved,"Cannot save independent acceptance fixture");
  }
  const auto before=Read(path);
  Require(ExportParts3mf(tree,false,true,path,false,error)==ExportResult::ConfirmOverwrite && Read(path)==before,"Overwrite confirmation damaged file");
  tree.state.flags.at("second").visible=false;
  Require(ExportParts3mf(tree,true,true,path,true,error)==ExportResult::Saved,"Visible export failed");
  Require(Count(Unzip(path).at("3D/3dmodel.model"),"<item ")==1,"Hidden instance exported in visible mode");
  tree.state.flags.at("first").visible=false;
  const auto visibleBefore=Read(path);
  Require(ExportParts3mf(tree,true,true,path,true,error)==ExportResult::Empty && Read(path)==visibleBefore,"Empty export damaged file");
  Require(ExportParts3mf(tree,false,true,path,true,error)==ExportResult::Saved && Count(Unzip(path).at("3D/3dmodel.model"),"<item ")==2,"All export omitted hidden parts");
  tree.parts[0].name=std::string("bad\0name",8);
  Require(ExportParts3mf(tree,false,true,path,true,error)==ExportResult::Failed && !error.empty() && Read(path)==before,"Serialization failure must preserve destination");
  tree.parts[0].name="okay";
  const auto blocker=root/"blocker"; {std::ofstream out(blocker);out<<"original";}
  Require(ExportParts3mf(tree,false,true,blocker/"child.3mf",true,error)==ExportResult::Failed && Read(blocker)=="original","Path failure damaged blocker");
  DisplayPart standalone=first; standalone.id="standalone"; standalone.sourceSolid.reset(); standalone.sourcePartId.clear();
  tree.Reload({standalone}); Require(ExportParts3mf(tree,false,true,path,true,error)==ExportResult::Saved,"Standalone export failed");
  const auto standaloneXml=Unzip(path).at("3D/3dmodel.model"); Require(standaloneXml.find("transform=")==std::string::npos,"Standalone placed geometry transformed twice");
  tree.Reload({}); Require(ExportParts3mf(tree,false,true,root/"empty.3mf",true,error)==ExportResult::Empty && !std::filesystem::exists(root/"empty.3mf"),"Empty tree wrote output");
  std::filesystem::remove_all(root);
  std::cout<<"PASS core 3MF ZIP/CRC, references, transforms, UTF-8, filtering and atomic failure preservation\n";
  return 0;
} catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;} }
