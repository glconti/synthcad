#pragma once
#include "appearance.h"
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace dingcad {
struct PartFlags { bool visible=true, exportable=true, exportOverride=false; };
struct TreeSession {
  std::unordered_map<std::string,PartFlags> flags;
  std::unordered_set<std::string> collapsed;
  std::string selected;
  bool isolated=false;
  std::unordered_map<std::string,bool> beforeIsolation;
};
struct PartNode {
  std::string key,name;
  bool group=false;
  int parent=-1;
  std::vector<size_t> children,parts;
  std::string sourceId;
};
struct TreeRow { size_t node; int depth; };
enum class CheckState { None, Mixed, All };
class PartTree {
 public:
  std::vector<DisplayPart> parts;
  std::vector<PartNode> nodes;
  TreeSession state;
  void Reload(std::vector<DisplayPart> next);
  std::vector<TreeRow> Rows(const std::string &query) const;
  CheckState Checked(size_t node,bool exporting) const;
  void Toggle(size_t node,bool exporting);
  void Select(size_t node);
  std::optional<size_t> Selection() const;
  void Isolate();
  void ShowAll();
  bool Visible(size_t part) const;
  std::vector<size_t> ExportIndices(bool visibleOnly) const;
  std::optional<manifold::Manifold> ExportSolid(bool visibleOnly) const;
 private:
  std::vector<size_t> roots_;
};
}
