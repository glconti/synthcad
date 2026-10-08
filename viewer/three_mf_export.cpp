#include "three_mf_export.h"
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace dingcad {
namespace {
std::string Xml(const std::string &value) {
  // Reject malformed UTF-8 and XML 1.0 forbidden characters before writing.
  std::string result;
  for (size_t i = 0; i < value.size();) {
    const size_t start = i;
    const unsigned char first = static_cast<unsigned char>(value[i++]);
    unsigned code = first, count = 0, minimum = 0;
    if (first >= 0xc2 && first <= 0xdf) { code = first & 31; count = 1; minimum = 0x80; }
    else if (first >= 0xe0 && first <= 0xef) { code = first & 15; count = 2; minimum = 0x800; }
    else if (first >= 0xf0 && first <= 0xf4) { code = first & 7; count = 3; minimum = 0x10000; }
    else if (first >= 0x80) throw std::runtime_error("Invalid UTF-8 in part name or identity");
    for (unsigned n = 0; n < count; ++n) {
      if (i == value.size() || (static_cast<unsigned char>(value[i]) & 0xc0) != 0x80)
        throw std::runtime_error("Invalid UTF-8 in part name or identity");
      code = (code << 6) | (static_cast<unsigned char>(value[i++]) & 63);
    }
    if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff) ||
        (code < 32 && code != 9 && code != 10 && code != 13) || code == 0xfffe || code == 0xffff)
      throw std::runtime_error("Forbidden XML character in part name or identity");
    if (code == '&') result += "&amp;";
    else if (code == '<') result += "&lt;";
    else if (code == '>') result += "&gt;";
    else if (code == '"') result += "&quot;";
    else if (code == '\'') result += "&apos;";
    else if (code == 9) result += "&#9;";
    else if (code == 10) result += "&#10;";
    else if (code == 13) result += "&#13;";
    else result.append(value, start, i - start);
  }
  return result;
}

void Number(std::ostream &out, double value) {
  if (!std::isfinite(value)) throw std::runtime_error("Non-finite geometry or transform");
  out << (value == 0 ? 0 : value);
}

std::string Transform(const DisplayPart &part) {
  constexpr double rad = 3.14159265358979323846 / 180;
  const double x = part.rotation[0]*rad, y = part.rotation[1]*rad, z = part.rotation[2]*rad;
  const double cx=std::cos(x), sx=std::sin(x), cy=std::cos(y), sy=std::sin(y), cz=std::cos(z), sz=std::sin(z);
  // 3MF stores the columns of Rz * Ry * Rx followed by translation.
  const double values[] = {cz*cy, sz*cy, -sy,
    cz*sy*sx-sz*cx, sz*sy*sx+cz*cx, cy*sx,
    cz*sy*cx+sz*sx, sz*sy*cx-cz*sx, cy*cx,
    part.translation[0], part.translation[1], part.translation[2]};
  std::ostringstream out; out.imbue(std::locale::classic()); out << std::setprecision(17);
  for (size_t i=0; i<12; ++i) { if(i) out << ' '; Number(out, values[i]); }
  return out.str();
}

uint32_t Crc(const std::string &data) {
  uint32_t crc=0xffffffff;
  for (unsigned char byte : data) {
    crc ^= byte;
    for (int bit=0; bit<8; ++bit) crc=(crc>>1) ^ (0xedb88320u & (0u-(crc&1)));
  }
  return ~crc;
}
void U16(std::string &out, uint16_t value) { out += char(value); out += char(value>>8); }
void U32(std::string &out, uint32_t value) { U16(out,uint16_t(value)); U16(out,uint16_t(value>>16)); }
uint32_t Size(size_t size) {
  if (size > std::numeric_limits<uint32_t>::max()) throw std::runtime_error("3MF exceeds supported ZIP32 size");
  return static_cast<uint32_t>(size);
}
std::string Zip(const std::vector<std::pair<std::string,std::string>> &entries) {
  std::string out, directory;
  for (const auto &[name,data] : entries) {
    const auto offset=Size(out.size()), size=Size(data.size()), crc=Crc(data);
    U32(out,0x04034b50); U16(out,20); U16(out,0x800); U16(out,0); U16(out,0); U16(out,33);
    U32(out,crc); U32(out,size); U32(out,size); U16(out,uint16_t(name.size())); U16(out,0); out+=name; out+=data;
    U32(directory,0x02014b50); U16(directory,20); U16(directory,20); U16(directory,0x800); U16(directory,0);
    U16(directory,0); U16(directory,33); U32(directory,crc); U32(directory,size); U32(directory,size);
    U16(directory,uint16_t(name.size())); U16(directory,0); U16(directory,0); U16(directory,0); U16(directory,0);
    U32(directory,0); U32(directory,offset); directory+=name;
  }
  const auto offset=Size(out.size()); out+=directory; Size(out.size());
  U32(out,0x06054b50); U16(out,0); U16(out,0); U16(out,uint16_t(entries.size())); U16(out,uint16_t(entries.size()));
  U32(out,Size(directory.size())); U32(out,offset); U16(out,0);
  return out;
}

// Exclusive creation and same-directory replacement avoid truncating an existing
// destination on serialization/write failures, and avoid no-overwrite races.
ExportResult Save(const std::filesystem::path &path, const std::string &data, bool overwrite, std::string &error) {
  static std::atomic<unsigned long long> sequence{0};
  auto temporary=path;
  temporary += ".tmp-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(sequence++);
#ifdef _WIN32
  HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(file==INVALID_HANDLE_VALUE) { error="Cannot create temporary 3MF file (Windows error "+std::to_string(GetLastError())+")"; return ExportResult::Failed; }
  bool okay=true;
  size_t offset=0;
  while(offset<data.size()) { DWORD written=0; const DWORD amount=static_cast<DWORD>(std::min<size_t>(data.size()-offset,1u<<30));
    if(!WriteFile(file,data.data()+offset,amount,&written,nullptr)||written!=amount){okay=false;break;} offset+=written; }
  if(okay) okay=FlushFileBuffers(file)!=0;
  if(!CloseHandle(file)) okay=false;
  if(okay) okay=MoveFileExW(temporary.c_str(),path.c_str(),overwrite?MOVEFILE_REPLACE_EXISTING:0)!=0;
  const DWORD failure=GetLastError();
  if(!okay) { DeleteFileW(temporary.c_str()); if(!overwrite && (failure==ERROR_FILE_EXISTS || failure==ERROR_ALREADY_EXISTS)) return ExportResult::ConfirmOverwrite;
    error="Cannot save 3MF (Windows error "+std::to_string(failure)+")"; return ExportResult::Failed; }
#else
  const int file=::open(temporary.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600);
  if(file<0) { error=std::strerror(errno); return ExportResult::Failed; }
  bool okay=true; size_t offset=0;
  while(offset<data.size()) { const auto written=::write(file,data.data()+offset,data.size()-offset);
    if(written<0 && errno==EINTR) continue;
    if(written<=0){okay=false;break;} offset+=static_cast<size_t>(written); }
  if(okay) okay=::fsync(file)==0;
  if(::close(file)!=0) okay=false;
  if(okay) okay=overwrite ? ::rename(temporary.c_str(),path.c_str())==0 : ::link(temporary.c_str(),path.c_str())==0;
  const int failure=errno;
  ::unlink(temporary.c_str());
  if(!okay) { if(!overwrite && failure==EEXIST) return ExportResult::ConfirmOverwrite;
    error=std::strerror(failure); return ExportResult::Failed; }
#endif
  return ExportResult::Saved;
}
}

ExportResult ExportParts3mf(const PartTree &tree, bool visibleOnly, bool valid,
                           const std::filesystem::path &path, bool overwrite, std::string &error) {
  error.clear();
  if(!valid) return ExportResult::Invalid;
  const auto indices=tree.ExportIndices(visibleOnly);
  if(indices.empty()) return ExportResult::Empty;
  try {
    std::error_code ec;
    const bool exists=std::filesystem::exists(path,ec);
    if(ec) throw std::runtime_error(ec.message());
    if(exists && !overwrite) return ExportResult::ConfirmOverwrite;
    std::ostringstream meshes, instances, build;
    for(auto *stream : {&meshes,&instances,&build}) { stream->imbue(std::locale::classic()); *stream << std::setprecision(17); }
    std::map<std::string,unsigned> sources;
    unsigned next=1, count=0;
    for(size_t index : indices) {
      const auto &part=tree.parts.at(index);
      const bool sourced=part.sourceSolid && !part.sourcePartId.empty();
      const auto solid=sourced ? part.sourceSolid : part.solid;
      if(!solid || solid->Status()!=manifold::Manifold::Error::NoError) throw std::runtime_error("Invalid export solid");
      const std::string key=sourced ? "source:"+part.sourcePartId : "instance:"+std::to_string(index);
      auto found=sources.find(key);
      unsigned meshId;
      if(found==sources.end()) {
        const auto mesh=solid->GetMeshGL64();
        if(mesh.NumTri()==0) continue;
        // MeshGL may duplicate a topological vertex at a property seam. Core
        // 3MF has no such properties here, so restore the authored connectivity
        // using Manifold's explicit merge map rather than coordinate welding.
        std::vector<size_t> canonical(mesh.NumVert());
        for(size_t v=0;v<canonical.size();++v) canonical[v]=v;
        for(size_t v=0;v<mesh.mergeFromVert.size();++v) canonical.at(mesh.mergeFromVert[v])=mesh.mergeToVert.at(v);
        for(size_t v=0;v<canonical.size();++v) {
          size_t target=v, steps=0;
          while(canonical.at(target)!=target) {target=canonical.at(target);if(++steps>canonical.size()) throw std::runtime_error("Cyclic mesh merge map");}
          canonical[v]=target;
        }
        std::map<size_t,size_t> vertices;
        for(auto v:mesh.triVerts) vertices.emplace(canonical.at(v),0);
        size_t vertexIndex=0;
        for(auto &entry:vertices) entry.second=vertexIndex++;
        meshId=next++; sources.emplace(key,meshId);
        meshes << "<object id=\"" << meshId << "\" type=\"model\" name=\"" << Xml(sourced?part.sourcePartId:part.name) << "\"><mesh><vertices>";
        for(const auto &entry:vertices) {
          const size_t vertex=entry.first;
          meshes << "<vertex x=\""; Number(meshes,mesh.vertProperties[vertex*mesh.numProp]);
          meshes << "\" y=\""; Number(meshes,mesh.vertProperties[vertex*mesh.numProp+1]);
          meshes << "\" z=\""; Number(meshes,mesh.vertProperties[vertex*mesh.numProp+2]); meshes << "\"/>";
        }
        meshes << "</vertices><triangles>";
        for(size_t tri=0;tri<mesh.NumTri();++tri) meshes << "<triangle v1=\"" << vertices.at(canonical.at(mesh.triVerts[tri*3])) << "\" v2=\"" << vertices.at(canonical.at(mesh.triVerts[tri*3+1])) << "\" v3=\"" << vertices.at(canonical.at(mesh.triVerts[tri*3+2])) << "\"/>";
        meshes << "</triangles></mesh></object>";
      } else meshId=found->second;
      const unsigned instanceId=next++; ++count;
      instances << "<object id=\"" << instanceId << "\" type=\"model\" name=\"" << Xml(part.name) << "\"><metadatagroup><metadata name=\"dingcad:instanceId\" preserve=\"true\">" << Xml(part.id) << "</metadata>";
      if(sourced) instances << "<metadata name=\"dingcad:sourcePartId\" preserve=\"true\">" << Xml(part.sourcePartId) << "</metadata>";
      instances << "</metadatagroup><components><component objectid=\"" << meshId << "\"";
      if(sourced) instances << " transform=\"" << Transform(part) << "\"";
      instances << "/></components></object>";
      build << "<item objectid=\"" << instanceId << "\"/>";
    }
    if(!count) return ExportResult::Empty;
    const std::string model="<?xml version=\"1.0\" encoding=\"UTF-8\"?><model unit=\"millimeter\" xml:lang=\"en-US\" xmlns=\"http://schemas.microsoft.com/3dmanufacturing/core/2015/02\" xmlns:dingcad=\"https://github.com/glconti/synthcad/3mf/metadata/1\"><metadata name=\"Application\">SynthCAD</metadata><resources>"+meshes.str()+instances.str()+"</resources><build>"+build.str()+"</build></model>";
    const std::string types="<?xml version=\"1.0\" encoding=\"UTF-8\"?><Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\"><Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/><Default Extension=\"model\" ContentType=\"application/vnd.ms-package.3dmanufacturing-3dmodel+xml\"/></Types>";
    const std::string relationships="<?xml version=\"1.0\" encoding=\"UTF-8\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"><Relationship Target=\"/3D/3dmodel.model\" Id=\"rel0\" Type=\"http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel\"/></Relationships>";
    const auto archive=Zip({{"[Content_Types].xml",types},{"_rels/.rels",relationships},{"3D/3dmodel.model",model}});
    if(!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(),ec);
    if(ec) throw std::runtime_error(ec.message());
    return Save(path,archive,overwrite,error);
  } catch(const std::exception &exception) { error=exception.what(); return ExportResult::Failed; }
}
}
