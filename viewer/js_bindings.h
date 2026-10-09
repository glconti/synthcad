#pragma once

extern "C" {
#include "quickjs.h"
}

#include <memory>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace manifold {
class Manifold;
}

void EnsureManifoldClass(JSRuntime *runtime);
// The callback must track binary bytes/missing paths in the evaluation's file
// snapshot. ImportMesh opens the same path independently: before/after checks
// detect ordinary concurrent changes, not an atomic proof of consumed bytes.
// Auxiliary files resolved internally by third-party importers are not exposed.
using MeshDependencyReader = std::function<std::optional<std::string>(const std::filesystem::path &)>;
void RegisterBindings(JSContext *ctx, MeshDependencyReader meshReader = {});
std::shared_ptr<manifold::Manifold> GetManifoldHandle(JSContext *ctx,
                                                      JSValueConst value);

#include <functional>
void SetBindingProgress(std::function<void(const char*)> callback);
