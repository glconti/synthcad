#pragma once
#include "part_tree.h"
#include <filesystem>
namespace dingcad {
enum class ExportResult { Saved, Empty, Invalid, ConfirmOverwrite, Failed };
ExportResult ExportParts(const PartTree &tree, bool visibleOnly, bool valid,
                         const std::filesystem::path &path, bool overwrite, std::string &error);
}
