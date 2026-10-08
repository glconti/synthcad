#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>

namespace synthcad {

// Persist one committed export receipt as an immutable JSON record beside the
// project. The result is {saved, path, error}; path is the history-record path.
nlohmann::json SaveExportRecord(const std::filesystem::path &projectPath,
                               const nlohmann::json &record);

// Read at most 1000 bounded receipt files. Damaged records are skipped and
// reported in diagnostics; loaded records include derived artifact/dependency
// status fields which are never written back to disk.
nlohmann::json LoadExportHistory(const std::filesystem::path &projectPath);

// Purely compare stored receipt basis fields with the supplied current context.
// Context fields are view, modelRevision, sourceRevision, layoutRevision, and
// profileRevision, plus optional current=false when the displayed scene was
// retained after a failed load. A different active view alone yields unknown;
// known source/profile changes yield stale.
nlohmann::json RefreshExportHistory(const nlohmann::json &history,
                                   const nlohmann::json &context);

// Limit cached publication to 4 MiB (records in supplied newest-first order).
// Omitted data is reported explicitly; immutable files remain on disk.
nlohmann::json PublishedExportHistory(const nlohmann::json &history);

} // namespace synthcad
