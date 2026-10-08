#pragma once
#include "stl_export.h"

namespace dingcad {
ExportResult ExportParts3mf(const PartTree &tree, bool visibleOnly, bool valid,
                           const std::filesystem::path &path, bool overwrite,
                           std::string &error);
}
