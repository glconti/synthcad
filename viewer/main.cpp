#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <vector>
#include <utility>

extern "C" {
#include "quickjs.h"
}

#include "manifold/manifold.h"
#include "manifold/polygon.h"
#include "js_bindings.h"
#include "dimensions.h"
#include "camera_controls.h"
#include "appearance.h"
#include "part_tree.h"
#include "parts_panel.h"
#include "stl_export.h"

namespace {
const Color kBaseColor = {210, 210, 220, 255};
const char *kBrandText = "dingcad";
constexpr float kBrandFontSize = 28.0f;
using dingcad::kSceneScale;
using dingcad::FrameScene;
using FrameClock = std::chrono::steady_clock;
constexpr int kFocusedFps = 60;
constexpr int kBackgroundFps = 15;
constexpr int kMinimizedFps = 5;
constexpr auto kSceneCheckInterval = std::chrono::milliseconds(250);
constexpr int kGridHalfLines = 40;
constexpr float kGridSpacing = 0.5f;
constexpr float kAxesLength = 2.0f;

// The post-processing pipeline works on the original macOS target, but the
// vcpkg raylib/OpenGL stack on Windows can hang in its first custom-shader
// draw.  Keep the Windows viewer responsive with raylib's default material
// while retaining the higher-fidelity pipeline on macOS.
#if defined(_WIN32)
constexpr bool kUsePostProcessing = false;
#else
constexpr bool kUsePostProcessing = true;
#endif

std::optional<std::filesystem::path> GetHomeDirectory() {
  for (const char *variable : {"HOME", "USERPROFILE"}) {
    if (const char *value = std::getenv(variable); value && *value) {
      return std::filesystem::path(value);
    }
  }
  return std::nullopt;
}

// GLSL 330 core (desktop). Uses raylib's default attribute/uniform names.
const char* kOutlineVS = R"glsl(
#version 330

in vec3 vertexPosition;
in vec3 vertexNormal;

uniform mat4 mvp;
uniform float outline;   // world-units thickness

void main()
{
    // Expand along the vertex normal in model space. This is robust as long as
    // your model transform has no non-uniform scale (true in your code).
    vec3 pos = vertexPosition + normalize(vertexNormal) * outline;
    gl_Position = mvp * vec4(pos, 1.0);
}
)glsl";

const char* kOutlineFS = R"glsl(
#version 330

out vec4 finalColor;
uniform vec4 outlineColor;

void main()
{
    // Keep only back-faces for a clean silhouette.
    if (gl_FrontFacing) discard;
    finalColor = outlineColor;
}
)glsl";

// Toon (cel) shading — lit 3D pass
const char* kToonVS = R"glsl(
#version 330
in vec3 vertexPosition;
in vec3 vertexNormal;
in vec4 vertexColor;
out vec4 vColor;
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matView;
out vec3 vNvs;
out vec3 vVdir; // view dir in view space
void main() {
    vColor = vertexColor;
    vec4 wpos = matModel * vec4(vertexPosition, 1.0);
    vec3 nvs  = mat3(matView) * mat3(matModel) * vertexNormal;
    vNvs      = normalize(nvs);
    vec3 vpos = (matView * wpos).xyz;
    vVdir     = normalize(-vpos);
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)glsl";

const char* kToonFS = R"glsl(
#version 330
in vec3 vNvs;
in vec3 vVdir;
in vec4 vColor;
out vec4 finalColor;

uniform vec3 lightDirVS;     // normalized, in view space
uniform vec4 baseColor;      // your kBaseColor normalized [0..1]
uniform int  toonSteps;      // e.g. 3 or 4
uniform float ambient;       // e.g. 0.3
uniform float diffuseWeight; // e.g. 0.7
uniform float rimWeight;     // e.g. 0.25
uniform float specWeight;    // e.g. 0.15
uniform float specShininess; // e.g. 32.0

float quantize(float x, int steps){
    float s = max(1, steps-1);
    return floor(clamp(x,0.0,1.0)*s + 1e-4)/s;
}

void main() {
    vec3 n   = normalize(vNvs);
    vec3 l   = normalize(lightDirVS);
    vec3 v   = normalize(vVdir);

    float ndl = max(0.0, dot(n,l));
    float cel = quantize(ndl, toonSteps);

    // crisp rim
    float rim = pow(1.0 - max(0.0, dot(n, v)), 1.5);

    // hard-edged spec
    float spec = pow(max(0.0, dot(reflect(-l, n), v)), specShininess);
    spec = step(0.5, spec) * specWeight;

    float shade = clamp(ambient + diffuseWeight*cel + rimWeight*rim + spec, 0.0, 1.0);
    finalColor  = vec4(vColor.rgb * shade, 1.0);
}
)glsl";

// Normal+Depth G-buffer — for screen-space edges
const char* kNormalDepthVS = R"glsl(
#version 330
in vec3 vertexPosition;
in vec3 vertexNormal;
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matView;
out vec3 nVS;
out float depthLin;
void main() {
    vec4 wpos = matModel * vec4(vertexPosition, 1.0);
    vec3 vpos = (matView * wpos).xyz;
    nVS = normalize(mat3(matView) * mat3(matModel) * vertexNormal);
    depthLin = -vpos.z; // linear view-space depth
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)glsl";

const char* kNormalDepthFS = R"glsl(
#version 330
in vec3 nVS;
in float depthLin;
out vec4 outColor;
uniform float zNear;
uniform float zFar;
void main() {
    float d = clamp((depthLin - zNear) / (zFar - zNear), 0.0, 1.0);
    outColor = vec4(nVS*0.5 + 0.5, d); // RGB: normal, A: linear depth
}
)glsl";

// Fullscreen composite — ink from normal/depth discontinuities
const char* kEdgeQuadVS = R"glsl(
#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
uniform mat4 mvp;
out vec2 uv;
void main() {
    uv = vertexTexCoord;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)glsl";

const char* kEdgeFS = R"glsl(
#version 330
in vec2 uv;
out vec4 finalColor;

uniform sampler2D texture0;      // color from toon pass
uniform sampler2D normDepthTex;  // RG: normal, A: depth from ND pass
uniform vec2 texel;              // 1/width, 1/height

uniform float normalThreshold;   // e.g. 0.25
uniform float depthThreshold;    // e.g. 0.002
uniform float edgeIntensity;     // e.g. 1.0
uniform vec4 inkColor;           // usually black

vec3 decodeN(vec3 c){ return normalize(c*2.0 - 1.0); }

void main(){
    vec4 col = texture(texture0, uv);
    vec4 nd  = texture(normDepthTex, uv);
    vec3 n   = decodeN(nd.rgb);
    float d  = nd.a;

    const vec2 offs[8] = vec2[](vec2(-1,-1), vec2(0,-1), vec2(1,-1),
                                vec2(-1, 0),              vec2(1, 0),
                                vec2(-1, 1), vec2(0, 1), vec2(1, 1));
    float maxNDiff = 0.0;
    float maxDDiff = 0.0;
    for (int i=0;i<8;i++){
        vec4 ndn = texture(normDepthTex, uv + offs[i]*texel);
        maxNDiff = max(maxNDiff, length(n - decodeN(ndn.rgb)));
        maxDDiff = max(maxDDiff, abs(d - ndn.a));
    }

    float eN = smoothstep(normalThreshold, normalThreshold*2.5, maxNDiff);
    float eD = smoothstep(depthThreshold,  depthThreshold*6.0,  maxDDiff);
    float edge = clamp(max(eN, eD)*edgeIntensity, 0.0, 1.0);

    vec3 inked = mix(col.rgb, inkColor.rgb, edge);
    finalColor = vec4(inked, col.a);
}
)glsl";

struct ModuleLoaderData {
  std::filesystem::path baseDir;
  std::set<std::filesystem::path> dependencies;
};

ModuleLoaderData g_module_loader_data;

struct WatchedFile {
  std::optional<std::filesystem::file_time_type> timestamp;
};

void DestroyModel(Model &model) {
  if (model.meshes != nullptr || model.materials != nullptr) {
    UnloadModel(model);
  }
  model = Model{};
}

Model CreateRaylibModelFrom(const manifold::MeshGL &meshGL, bool vertexColors=false, bool bakeLighting=true) {
  Model model = {0};
  const int vertexCount = meshGL.NumVert();
  const int triangleCount = meshGL.NumTri();

  if (vertexCount <= 0 || triangleCount <= 0) {
    return model;
  }

  const int stride = meshGL.numProp;
  std::vector<Vector3> positions(vertexCount);
  for (int v = 0; v < vertexCount; ++v) {
    const int base = v * stride;
    // Convert from the scene's Z-up coordinates to raylib's Y-up system.
    const float cadX = meshGL.vertProperties[base + 0] * kSceneScale;
    const float cadY = meshGL.vertProperties[base + 1] * kSceneScale;
    const float cadZ = meshGL.vertProperties[base + 2] * kSceneScale;
    positions[v] = {cadX, cadZ, -cadY};
  }

  std::vector<Vector3> accum(vertexCount, {0.0f, 0.0f, 0.0f});
  for (int tri = 0; tri < triangleCount; ++tri) {
    const int i0 = meshGL.triVerts[tri * 3 + 0];
    const int i1 = meshGL.triVerts[tri * 3 + 1];
    const int i2 = meshGL.triVerts[tri * 3 + 2];

    const Vector3 p0 = positions[i0];
    const Vector3 p1 = positions[i1];
    const Vector3 p2 = positions[i2];

    const Vector3 u = {p1.x - p0.x, p1.y - p0.y, p1.z - p0.z};
    const Vector3 v = {p2.x - p0.x, p2.y - p0.y, p2.z - p0.z};
    const Vector3 n = {u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z,
                       u.x * v.y - u.y * v.x};

    accum[i0].x += n.x;
    accum[i0].y += n.y;
    accum[i0].z += n.z;
    accum[i1].x += n.x;
    accum[i1].y += n.y;
    accum[i1].z += n.z;
    accum[i2].x += n.x;
    accum[i2].y += n.y;
    accum[i2].z += n.z;
  }

  std::vector<Vector3> normals(vertexCount);
  std::vector<Color> colors(vertexCount);
  const Vector3 lightDir = Vector3Normalize({0.3f, 0.85f, -0.4f});
  for (int v = 0; v < vertexCount; ++v) {
    const Vector3 n = accum[v];
    const float length = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);

    Vector3 normal = {0.0f, 1.0f, 0.0f};
    if (length > 0.0f) {
      normal = {n.x / length, n.y / length, n.z / length};
    }
    normals[v] = normal;

    float intensity = Vector3DotProduct(normal, lightDir);
    intensity = Clamp(intensity, 0.0f, 1.0f);
    const float finalIntensity = bakeLighting ? 0.55f + 0.45f * intensity : 1.0f;
    const Color base = vertexColors ? Color{
      static_cast<unsigned char>(meshGL.vertProperties[v*stride+3]),
      static_cast<unsigned char>(meshGL.vertProperties[v*stride+4]),
      static_cast<unsigned char>(meshGL.vertProperties[v*stride+5]),255} : kBaseColor;
    Color color = {0};
    color.r = static_cast<unsigned char>(
        Clamp(base.r * finalIntensity, 0.0f, 255.0f));
    color.g = static_cast<unsigned char>(
        Clamp(base.g * finalIntensity, 0.0f, 255.0f));
    color.b = static_cast<unsigned char>(
        Clamp(base.b * finalIntensity, 0.0f, 255.0f));
    color.a = base.a;
    colors[v] = color;
  }

  constexpr int kMaxVerticesPerMesh = std::numeric_limits<unsigned short>::max();
  std::vector<int> remap(vertexCount, 0);
  std::vector<int> remapMarker(vertexCount, 0);
  int chunkToken = 1;

  std::vector<Mesh> meshes;
  meshes.reserve(
      static_cast<size_t>(triangleCount) / kMaxVerticesPerMesh + 1);

  int triIndex = 0;
  while (triIndex < triangleCount) {
    const int currentToken = chunkToken++;
    int chunkVertexCount = 0;
    std::vector<Vector3> chunkPositions;
    std::vector<Vector3> chunkNormals;
    std::vector<Color> chunkColors;
    std::vector<unsigned short> chunkIndices;

    chunkPositions.reserve(std::min(kMaxVerticesPerMesh, vertexCount));
    chunkNormals.reserve(std::min(kMaxVerticesPerMesh, vertexCount));
    chunkColors.reserve(std::min(kMaxVerticesPerMesh, vertexCount));
    chunkIndices.reserve(std::min(kMaxVerticesPerMesh, vertexCount) * 3);

    while (triIndex < triangleCount) {
      const int indices[3] = {
          static_cast<int>(meshGL.triVerts[triIndex * 3 + 0]),
          static_cast<int>(meshGL.triVerts[triIndex * 3 + 1]),
          static_cast<int>(meshGL.triVerts[triIndex * 3 + 2])};

      int needed = 0;
      for (int j = 0; j < 3; ++j) {
        if (remapMarker[indices[j]] != currentToken) {
          ++needed;
        }
      }

      if (chunkVertexCount + needed > kMaxVerticesPerMesh) {
        break;
      }

      for (int j = 0; j < 3; ++j) {
        const int original = indices[j];
        if (remapMarker[original] != currentToken) {
          remapMarker[original] = currentToken;
          remap[original] = chunkVertexCount++;
          chunkPositions.push_back(positions[original]);
          chunkNormals.push_back(normals[original]);
          chunkColors.push_back(colors[original]);
        }
        chunkIndices.push_back(static_cast<unsigned short>(remap[original]));
      }
      ++triIndex;
    }

    Mesh chunkMesh = {0};
    chunkMesh.vertexCount = chunkVertexCount;
    chunkMesh.triangleCount = static_cast<int>(chunkIndices.size() / 3);
    chunkMesh.vertices = static_cast<float *>(
        MemAlloc(chunkVertexCount * 3 * sizeof(float)));
    chunkMesh.normals = static_cast<float *>(
        MemAlloc(chunkVertexCount * 3 * sizeof(float)));
    chunkMesh.colors = static_cast<unsigned char *>(
        MemAlloc(chunkVertexCount * 4 * sizeof(unsigned char)));
    chunkMesh.indices = static_cast<unsigned short *>(
        MemAlloc(chunkIndices.size() * sizeof(unsigned short)));
    chunkMesh.texcoords = nullptr;
    chunkMesh.texcoords2 = nullptr;
    chunkMesh.tangents = nullptr;

    for (int v = 0; v < chunkVertexCount; ++v) {
      const Vector3 &pos = chunkPositions[v];
      chunkMesh.vertices[v * 3 + 0] = pos.x;
      chunkMesh.vertices[v * 3 + 1] = pos.y;
      chunkMesh.vertices[v * 3 + 2] = pos.z;

      const Vector3 &normal = chunkNormals[v];
      chunkMesh.normals[v * 3 + 0] = normal.x;
      chunkMesh.normals[v * 3 + 1] = normal.y;
      chunkMesh.normals[v * 3 + 2] = normal.z;

      const Color color = chunkColors[v];
      chunkMesh.colors[v * 4 + 0] = color.r;
      chunkMesh.colors[v * 4 + 1] = color.g;
      chunkMesh.colors[v * 4 + 2] = color.b;
      chunkMesh.colors[v * 4 + 3] = color.a;
    }

    std::memcpy(chunkMesh.indices, chunkIndices.data(),
                chunkIndices.size() * sizeof(unsigned short));
    UploadMesh(&chunkMesh, false);
    meshes.push_back(chunkMesh);
  }

  if (meshes.empty()) {
    return model;
  }

  model.transform = MatrixIdentity();
  model.meshCount = static_cast<int>(meshes.size());
  model.meshes = static_cast<Mesh *>(
      MemAlloc(model.meshCount * sizeof(Mesh)));
  for (int i = 0; i < model.meshCount; ++i) {
    model.meshes[i] = meshes[i];
  }
  model.materialCount = 1;
  model.materials = static_cast<Material *>(MemAlloc(sizeof(Material)));
  model.materials[0] = LoadMaterialDefault();
  model.meshMaterial = static_cast<int *>(
      MemAlloc(model.meshCount * sizeof(int)));
  for (int i = 0; i < model.meshCount; ++i) {
    model.meshMaterial[i] = 0;
  }

  return model;
}

void DrawAxes(float length) {
  const float shaftRadius = std::max(length * 0.02f, 0.01f);
  const float headLength = std::min(length * 0.2f, length * 0.75f);
  const float headRadius = shaftRadius * 2.5f;

  auto drawAxis = [&](Vector3 direction, Color color) {
    const Vector3 origin = {0.0f, 0.0f, 0.0f};
    const float shaftLength = std::max(length - headLength, 0.0f);
    const Vector3 shaftEnd = Vector3Scale(direction, shaftLength);
    const Vector3 axisEnd = Vector3Scale(direction, length);

    if (shaftLength > 0.0f) {
      DrawCylinderEx(origin, shaftEnd, shaftRadius, shaftRadius, 12, Fade(color, 0.65f));
    }
    DrawCylinderEx(shaftEnd, axisEnd, headRadius, 0.0f, 16, color);
  };

  drawAxis({1.0f, 0.0f, 0.0f}, RED);    // +X
  drawAxis({0.0f, 1.0f, 0.0f}, GREEN);  // +Y
  drawAxis({0.0f, 0.0f, 1.0f}, BLUE);   // +Z

  DrawSphereEx({0.0f, 0.0f, 0.0f}, shaftRadius * 1.2f, 12, 12, LIGHTGRAY);
}

void DrawXZGrid(int halfLines, float spacing, Color color) {
  for (int i = -halfLines; i <= halfLines; ++i) {
    const float offset = static_cast<float>(i) * spacing;
    DrawLine3D({offset, 0.0f, -halfLines * spacing},
               {offset, 0.0f, halfLines * spacing}, color);
    DrawLine3D({-halfLines * spacing, 0.0f, offset},
               {halfLines * spacing, 0.0f, offset}, color);
  }
}

BoundingBox SceneRenderBounds(const Model &model) {
  // Include the grid and the full axis geometry even for an empty scene.
  constexpr float extent = kGridHalfLines * kGridSpacing;
  BoundingBox bounds{{-extent, -0.1f, -extent}, {extent, kAxesLength, extent}};
  if (model.meshCount > 0) {
    const BoundingBox modelBounds = GetModelBoundingBox(model);
    bounds.min = Vector3Min(bounds.min, modelBounds.min);
    bounds.max = Vector3Max(bounds.max, modelBounds.max);
  }
  return bounds;
}

std::optional<std::filesystem::path> FindDefaultScene() {
  auto cwdCandidate = std::filesystem::current_path() / "scene.js";
  if (std::filesystem::exists(cwdCandidate)) return cwdCandidate;
  if (const auto home = GetHomeDirectory()) {
    std::filesystem::path homeCandidate = *home / "scene.js";
    if (std::filesystem::exists(homeCandidate)) return homeCandidate;
  }
  return std::nullopt;
}

std::optional<std::string> ReadTextFile(const std::filesystem::path &path) {
  std::ifstream file(path);
  if (!file) return std::nullopt;
  std::ostringstream ss;
  ss << file.rdbuf();
  return ss.str();
}

JSModuleDef *FilesystemModuleLoader(JSContext *ctx, const char *module_name, void *opaque) {
  auto *data = static_cast<ModuleLoaderData *>(opaque);
  std::filesystem::path resolved(module_name);
  if (resolved.is_relative()) {
    const std::filesystem::path base = data && !data->baseDir.empty()
                                           ? data->baseDir
                                           : std::filesystem::current_path();
    resolved = base / resolved;
  }
  resolved = std::filesystem::absolute(resolved).lexically_normal();

  if (data) {
    data->baseDir = resolved.parent_path();
    data->dependencies.insert(resolved);
  }

  auto source = ReadTextFile(resolved);
  if (!source) {
    JS_ThrowReferenceError(ctx, "Unable to load module '%s'", resolved.string().c_str());
    return nullptr;
  }

  const std::string moduleName = resolved.string();
  JSValue funcVal = JS_Eval(ctx, source->c_str(), source->size(), moduleName.c_str(),
                            JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
  if (JS_IsException(funcVal)) {
    return nullptr;
  }

  auto *module = static_cast<JSModuleDef *>(JS_VALUE_GET_PTR(funcVal));
  JS_FreeValue(ctx, funcVal);
  return module;
}

double ElapsedMilliseconds(std::chrono::steady_clock::time_point start) {
  return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
}

struct LoadResult {
  bool success = false;
  double loadMilliseconds=0, evaluationMilliseconds=0;
  std::shared_ptr<manifold::Manifold> manifold;
  std::string message;
  std::vector<std::filesystem::path> dependencies;
  std::vector<dingcad::Dimension> dimensions;
  dingcad::Appearance appearance;
};

LoadResult LoadSceneFromFile(JSRuntime *runtime, const std::filesystem::path &path) {
  LoadResult result;
  const auto loadStarted=std::chrono::steady_clock::now();
  const auto absolutePath = std::filesystem::absolute(path);
  if (!std::filesystem::exists(absolutePath)) {
    result.message = "Scene file not found: " + absolutePath.string();
    return result;
  }
  g_module_loader_data.baseDir = absolutePath.parent_path();
  g_module_loader_data.dependencies.clear();
  g_module_loader_data.dependencies.insert(absolutePath);
  auto sourceOpt = ReadTextFile(absolutePath);
  if (!sourceOpt) {
    result.message = "Unable to read scene file: " + absolutePath.string();
    result.dependencies.assign(g_module_loader_data.dependencies.begin(),
                               g_module_loader_data.dependencies.end());
    return result;
  }
  JSContext *ctx = JS_NewContext(runtime);
  RegisterBindings(ctx);

  auto captureException = [&]() {
    JSValue exc = JS_GetException(ctx);
    JSValue stack = JS_GetPropertyStr(ctx, exc, "stack");
    const char *stackStr = JS_ToCString(ctx, JS_IsUndefined(stack) ? exc : stack);
    result.message = stackStr ? stackStr : "JavaScript error";
    JS_FreeCString(ctx, stackStr);
    JS_FreeValue(ctx, stack);
    JS_FreeValue(ctx, exc);
  };
  auto assignDependencies = [&]() {
    result.dependencies.assign(g_module_loader_data.dependencies.begin(),
                               g_module_loader_data.dependencies.end());
  };

  JSValue moduleFunc = JS_Eval(ctx, sourceOpt->c_str(), sourceOpt->size(), absolutePath.string().c_str(),
                               JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
  if (JS_IsException(moduleFunc)) {
    captureException();
    assignDependencies();
    JS_FreeContext(ctx);
    return result;
  }

  if (JS_ResolveModule(ctx, moduleFunc) < 0) {
    captureException();
    JS_FreeValue(ctx, moduleFunc);
    assignDependencies();
    JS_FreeContext(ctx);
    return result;
  }

  auto *module = static_cast<JSModuleDef *>(JS_VALUE_GET_PTR(moduleFunc));
  const auto evaluationStarted=std::chrono::steady_clock::now();
  JSValue evalResult = JS_EvalFunction(ctx, moduleFunc);
  result.evaluationMilliseconds=ElapsedMilliseconds(evaluationStarted);
  if (JS_IsException(evalResult)) {
    captureException();
    assignDependencies();
    JS_FreeContext(ctx);
    return result;
  }
  JS_FreeValue(ctx, evalResult);

  JSValue moduleNamespace = JS_GetModuleNamespace(ctx, module);
  if (JS_IsException(moduleNamespace)) {
    captureException();
    assignDependencies();
    JS_FreeContext(ctx);
    return result;
  }

  JSValue sceneVal = JS_GetPropertyStr(ctx, moduleNamespace, "scene");
  if (JS_IsException(sceneVal)) {
    JS_FreeValue(ctx, moduleNamespace);
    captureException();
    assignDependencies();
    JS_FreeContext(ctx);
    return result;
  }
  auto annotations = dingcad::ReadDimensions(ctx, moduleNamespace);
  result.appearance = dingcad::ReadAppearance(ctx, moduleNamespace);
  JS_FreeValue(ctx, moduleNamespace);

  if(!result.appearance.diagnostic.empty()){
    result.message=result.appearance.diagnostic;
    JS_FreeValue(ctx,sceneVal);JS_FreeContext(ctx);assignDependencies();return result;
  }

  if (JS_IsUndefined(sceneVal)) {
    JS_FreeValue(ctx, sceneVal);
    JS_FreeContext(ctx);
    result.message = "Scene module must export 'scene'";
    assignDependencies();
    return result;
  }

  auto sceneHandle = GetManifoldHandle(ctx, sceneVal);
  if (!sceneHandle) {
    JS_FreeValue(ctx, sceneVal);
    JS_FreeContext(ctx);
    result.message = "Exported 'scene' is not a manifold";
    assignDependencies();
    return result;
  }

  result.manifold = sceneHandle;
  result.success = true;
  result.message = "Loaded " + absolutePath.string();
  result.dimensions = std::move(annotations.entries);
  for (const auto &diagnostic : annotations.diagnostics) {
    TraceLog(LOG_WARNING, "%s", diagnostic.c_str());
  }
  if (!annotations.diagnostics.empty()) {
    result.message += " (" + std::to_string(annotations.diagnostics.size()) + " dimension warning(s))";
  }
  assignDependencies();
  JS_FreeValue(ctx, sceneVal);
  JS_FreeContext(ctx);
  result.loadMilliseconds=ElapsedMilliseconds(loadStarted);
  return result;
}

std::vector<dingcad::DisplayPart> SceneParts(const std::shared_ptr<manifold::Manifold> &scene,
                                           const dingcad::Appearance &appearance){
  if(appearance.specified)return appearance.parts;
  return {{scene,kBaseColor,"@scene","Scena",{},true}};
}
struct PartModels {
  std::vector<Model> models;
  std::vector<BoundingBox> bounds;
  void Clear(){for(auto &m:models)DestroyModel(m);models.clear();bounds.clear();}
  void Reload(const dingcad::PartTree &tree){
    const auto started=std::chrono::steady_clock::now();
    Clear();for(const auto &part:tree.parts){
      models.push_back(CreateRaylibModelFrom(dingcad::DisplayMesh({part}),true,!kUsePostProcessing));
      bounds.push_back(GetModelBoundingBox(models.back()));
    }
    TraceLog(LOG_INFO,"Display mesh conversion and GPU upload: %.1f ms",ElapsedMilliseconds(started));
  }
  std::optional<BoundingBox> Bounds(const dingcad::PartTree &tree,bool selected=false) const{
    std::optional<BoundingBox> result;const auto node=tree.Selection();
    if(selected&&!node)return result;
    for(size_t i=0;i<models.size();++i){
      if(!tree.Visible(i)||models[i].meshCount==0)continue;
      if(selected&&std::find(tree.nodes[*node].parts.begin(),tree.nodes[*node].parts.end(),i)==tree.nodes[*node].parts.end())continue;
      const auto &b=bounds[i];
      if(result){result->min=Vector3Min(result->min,b.min);result->max=Vector3Max(result->max,b.max);}else result=b;
    }
    return result;
  }
  void Draw(const dingcad::PartTree &tree,const Material *material=nullptr) const{
    for(size_t p=0;p<models.size();++p)if(tree.Visible(p)){
      const auto &m=models[p];
      if(material){for(int i=0;i<m.meshCount;++i)DrawMesh(m.meshes[i],*material,m.transform);}
      else DrawModel(m,{0,0,0},1,WHITE);
    }
  }
};

}  // namespace

int main(int argc, char *argv[]) {
  // Deterministic preview using the same mesh colors, creases and default
  // material as the interactive Windows view. Never exports or changes solids.
  if (argc==4 && std::string(argv[1])=="--render-scene") {
    JSRuntime *runtime=JS_NewRuntime();EnsureManifoldClass(runtime);
    JS_SetModuleLoaderFunc(runtime,nullptr,FilesystemModuleLoader,&g_module_loader_data);
    auto load=LoadSceneFromFile(runtime,argv[2]);
    if (!load.success) {std::cerr<<load.message<<'\n';JS_FreeRuntime(runtime);return 1;}
    SetConfigFlags(FLAG_WINDOW_HIDDEN|FLAG_MSAA_4X_HINT);InitWindow(1600,1000,"dingcad preview");
    auto mesh=dingcad::DisplayMesh(!load.appearance.specified?
      std::vector<dingcad::DisplayPart>{{load.manifold,kBaseColor}}:load.appearance.parts);
    Model model=CreateRaylibModelFrom(mesh,true,true);
    const auto bounds=model.meshCount>0?GetModelBoundingBox(model):SceneRenderBounds(model);
    const auto camera=FrameScene(bounds,1600,1000);
    rlSetClipPlanes(0.01,dingcad::CameraFarClip(camera.position,bounds,1000));
    bool saved=false;
    for (int frame=0;frame<3;++frame){
      BeginDrawing();ClearBackground({242,242,236,255});BeginMode3D(camera);
      DrawModel(model,{0,0,0},1,WHITE);EndMode3D();
      rlDrawRenderBatchActive();
      if(frame==2){Image preview=LoadImageFromScreen();saved=ExportImage(preview,argv[3]);UnloadImage(preview);}
      EndDrawing();
    }
    DestroyModel(model);CloseWindow();load.appearance.parts.clear();load.manifold.reset();JS_FreeRuntime(runtime);
    return saved?0:1;
  }
  // Separate eager model evaluation from deferred tessellation. This headless
  // diagnostic never writes geometry and uses the same parts as the viewer.
  if(argc==3&&std::string(argv[1])=="--profile-scene"){
    const auto started=std::chrono::steady_clock::now();
    JSRuntime *runtime=JS_NewRuntime();EnsureManifoldClass(runtime);
    JS_SetModuleLoaderFunc(runtime,nullptr,FilesystemModuleLoader,&g_module_loader_data);
    auto load=LoadSceneFromFile(runtime,argv[2]);
    if(!load.success){std::cerr<<load.message<<'\n';JS_FreeRuntime(runtime);return 1;}
    const auto meshStarted=std::chrono::steady_clock::now();
    size_t triangles=0;
    const auto parts=SceneParts(load.manifold,load.appearance);
    for(const auto &part:parts)triangles+=dingcad::DisplayMesh({part}).NumTri();
    const double meshMilliseconds=ElapsedMilliseconds(meshStarted);
    std::cout<<"Scene load (ms): "<<load.loadMilliseconds<<'\n'
             <<"  JavaScript/model evaluation (ms): "<<load.evaluationMilliseconds<<'\n'
             <<"Display mesh conversion (ms): "<<meshMilliseconds<<'\n'
             <<"Total before GPU (ms): "<<ElapsedMilliseconds(started)<<'\n'
             <<"Parts: "<<parts.size()<<", triangles: "<<triangles<<'\n';
    JS_FreeRuntime(runtime);return 0;
  }
  // Validate scene code and annotations without opening a window, useful for
  // agents and automated checks before replacing the live scene.
  if (argc == 3 && std::string(argv[1]) == "--check-scene") {
    JSRuntime *runtime = JS_NewRuntime();
    EnsureManifoldClass(runtime);
    JS_SetModuleLoaderFunc(runtime, nullptr, FilesystemModuleLoader, &g_module_loader_data);
    auto load = LoadSceneFromFile(runtime, argv[2]);
    std::cout << load.message << '\n';
    if (load.success) {
      const auto bounds = load.manifold->BoundingBox();
      std::cout << "Bounds (mm): " << bounds.Size().x << " x " << bounds.Size().y << " x " << bounds.Size().z << '\n';
      for (const auto &dimension : load.dimensions) std::cout << dingcad::FormatDimension(dimension) << '\n';
      std::cout << "Colored display parts: " << load.appearance.parts.size() << '\n';
    }
    load.manifold.reset();
    JS_FreeRuntime(runtime);
    return load.success ? 0 : 1;
  }
  // Keep event polling nonblocking while minimized so live reload still runs.
  const bool uiPreview=argc>=4 && std::string(argv[1])=="--ui-preview";
  int previewFrames=0;
  SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_ALWAYS_RUN | (uiPreview?FLAG_WINDOW_HIDDEN:0));
  InitWindow(1280, 720, "dingcad");
  SetWindowMinSize(640,400);
  // Pace frames ourselves: this raylib build's WaitTime() spins a CPU core.
  SetTargetFPS(0);
  SetExitKey(KEY_NULL);

  Font brandingFont = GetFontDefault();
  bool brandingFontCustom = false;
#if defined(_WIN32)
  const std::filesystem::path uiFontPath("C:/Windows/Fonts/segoeui.ttf");
#else
  const std::filesystem::path uiFontPath("/System/Library/Fonts/Supplemental/Arial.ttf");
#endif
  if (std::filesystem::exists(uiFontPath)) {
    int glyphs[224];for(int i=0;i<224;++i)glyphs[i]=32+i;
    brandingFont = LoadFontEx(uiFontPath.string().c_str(),32,glyphs,224);
    SetTextureFilter(brandingFont.texture,TEXTURE_FILTER_BILINEAR);
    brandingFontCustom = true;
  }

  Camera3D camera = {0};
  camera.position = {4.0f, 4.0f, 4.0f};
  camera.target = {0.0f, 0.5f, 0.0f};
  camera.up = {0.0f, 1.0f, 0.0f};
  camera.fovy = 45.0f;
  camera.projection = CAMERA_PERSPECTIVE;

  float orbitDistance = Vector3Distance(camera.position, camera.target);
  float orbitYaw = atan2f(camera.position.x - camera.target.x,
                          camera.position.z - camera.target.z);
  float orbitPitch = asinf((camera.position.y - camera.target.y) / orbitDistance);

  JSRuntime *runtime = JS_NewRuntime();
  EnsureManifoldClass(runtime);
  JS_SetModuleLoaderFunc(runtime, nullptr, FilesystemModuleLoader, &g_module_loader_data);

  std::shared_ptr<manifold::Manifold> scene = nullptr;
  std::vector<dingcad::Dimension> dimensions;
  dingcad::Appearance appearance;
  dingcad::DimensionControls dimensionControls;
  std::string statusMessage;
  bool exportValid=false;
  std::filesystem::path scriptPath;
  std::unordered_map<std::filesystem::path, WatchedFile> watchedFiles;
  std::optional<std::filesystem::path> defaultScript;
  if (argc > 1) {
    defaultScript = std::filesystem::path(argv[uiPreview?2:1]);
  } else {
    defaultScript = FindDefaultScene();
  }
  auto reportStatus = [&](const std::string &message) {
    statusMessage = message;
    TraceLog(LOG_INFO, "%s", statusMessage.c_str());
    std::cout << statusMessage << std::endl;
  };
  auto setWatchedFiles = [&](const std::vector<std::filesystem::path> &deps) {
    std::unordered_map<std::filesystem::path, WatchedFile> updated;
    for (const auto &dep : deps) {
      WatchedFile entry;
      std::error_code ec;
      auto ts = std::filesystem::last_write_time(dep, ec);
      if (!ec) {
        entry.timestamp = ts;
      }
      updated.emplace(dep, entry);
    }
    watchedFiles = std::move(updated);
  };
  if (defaultScript) {
    scriptPath = std::filesystem::absolute(*defaultScript);
    auto load = LoadSceneFromFile(runtime, scriptPath);
    if (load.success) {
      scene = load.manifold;
      exportValid=true;
      dimensions = std::move(load.dimensions);
      appearance = std::move(load.appearance);
      reportStatus(load.message);
      TraceLog(LOG_INFO,"Scene load: %.1f ms (model evaluation %.1f ms)",load.loadMilliseconds,load.evaluationMilliseconds);
    } else {
      reportStatus(load.message);
    }
    if (!load.dependencies.empty()) {
      setWatchedFiles(load.dependencies);
    }
  }
  if (!scene) {
    manifold::Manifold cube = manifold::Manifold::Cube({2.0, 2.0, 2.0}, true);
    manifold::Manifold sphere = manifold::Manifold::Sphere(1.2, 0);
    manifold::Manifold combo = cube + sphere.Translate({0.0, 0.8, 0.0});
    scene = std::make_shared<manifold::Manifold>(combo);
    if (!defaultScript) exportValid=true;
    if (statusMessage.empty()) {
      reportStatus("No scene.js found. Using built-in sample.");
    }
  }

  dingcad::PartTree tree;
  tree.Reload(SceneParts(scene,appearance));
  appearance.parts.clear();
  PartModels partModels;partModels.Reload(tree);
  dingcad::PartsPanel panel;
  dingcad::ExportDialog exportDialog;
  std::unordered_map<std::string,dingcad::TreeSession> sessions;
  std::string liveSceneKey=scriptPath.string();
  auto viewport=panel.Viewport(GetScreenWidth(),GetScreenHeight());
  BoundingBox renderBounds{{-20,-0.1f,-20},{20,2,20}};
  auto updateBounds=[&](){
    renderBounds={{-20,-0.1f,-20},{20,2,20}};
    if(auto b=partModels.Bounds(tree)){renderBounds.min=Vector3Min(renderBounds.min,b->min);renderBounds.max=Vector3Max(renderBounds.max,b->max);}
  };
  auto frameParts=[&](bool selected){
    if(auto b=partModels.Bounds(tree,selected)){
      camera=FrameScene(*b,static_cast<int>(viewport.width),static_cast<int>(viewport.height));
      orbitDistance=Vector3Distance(camera.position,camera.target);
      orbitYaw=atan2f(camera.position.x-camera.target.x,camera.position.z-camera.target.z);
      orbitPitch=asinf((camera.position.y-camera.target.y)/orbitDistance);
    }
  };
  updateBounds();frameParts(false);
  const auto defaultExportPath=(GetHomeDirectory().value_or(std::filesystem::current_path())/"Downloads"/"ding.stl").u8string();
  if(uiPreview&&argc>4){
    const std::string mode=argv[4];
    if(mode=="closed")panel.open=false;
    if(mode=="small")SetWindowSize(720,480);
    if(mode=="selected"||mode=="isolated"){
      for(size_t n=0;n<tree.nodes.size();++n)if(tree.nodes[n].name=="Oggetto progettato")tree.Select(n);
      if(mode=="isolated")tree.Isolate();
    }
    if(mode=="dimensions")dimensionControls.mode=dingcad::DimensionMode::All;
    if(mode=="export")exportDialog.Open(defaultExportPath);
    if(mode=="hidden")for(auto &[_,f]:tree.state.flags)f.visible=false;
  }

  Shader outlineShader = LoadShaderFromMemory(kOutlineVS, kOutlineFS);
  Shader toonShader = LoadShaderFromMemory(kToonVS, kToonFS);
  Shader normalDepthShader = LoadShaderFromMemory(kNormalDepthVS, kNormalDepthFS);
  Shader edgeShader = LoadShaderFromMemory(kEdgeQuadVS, kEdgeFS);

  if (outlineShader.id == 0 || toonShader.id == 0 || normalDepthShader.id == 0 || edgeShader.id == 0) {
    TraceLog(LOG_ERROR, "Failed to load one or more shaders.");
    partModels.Clear();
    if (brandingFontCustom) {
      UnloadFont(brandingFont);
    }
    JS_FreeRuntime(runtime);
    CloseWindow();
    return 1;
  }

  // Outline uniforms/material
  const int locOutline = GetShaderLocation(outlineShader, "outline");
  const int locOutlineColor = GetShaderLocation(outlineShader, "outlineColor");
  Material outlineMat = LoadMaterialDefault();
  outlineMat.shader = outlineShader;

  auto setOutlineUniforms = [&](float worldThickness, Color color) {
    float c[4] = {
        color.r / 255.0f,
        color.g / 255.0f,
        color.b / 255.0f,
        color.a / 255.0f};
    SetShaderValue(outlineMat.shader, locOutline, &worldThickness, SHADER_UNIFORM_FLOAT);
    SetShaderValue(outlineMat.shader, locOutlineColor, c, SHADER_UNIFORM_VEC4);
  };

  // Toon shader uniforms/material
  const int locLightDirVS = GetShaderLocation(toonShader, "lightDirVS");
  const int locBaseColor = GetShaderLocation(toonShader, "baseColor");
  const int locToonSteps = GetShaderLocation(toonShader, "toonSteps");
  const int locAmbient = GetShaderLocation(toonShader, "ambient");
  const int locDiffuseWeight = GetShaderLocation(toonShader, "diffuseWeight");
  const int locRimWeight = GetShaderLocation(toonShader, "rimWeight");
  const int locSpecWeight = GetShaderLocation(toonShader, "specWeight");
  const int locSpecShininess = GetShaderLocation(toonShader, "specShininess");
  Material toonMat = LoadMaterialDefault();
  toonMat.shader = toonShader;

  // Normal/depth shader uniforms/material
  const int locNear = GetShaderLocation(normalDepthShader, "zNear");
  const int locFar = GetShaderLocation(normalDepthShader, "zFar");
  Material normalDepthMat = LoadMaterialDefault();
  normalDepthMat.shader = normalDepthShader;

  // Edge composite uniforms
  const int locNormDepthTexture = GetShaderLocation(edgeShader, "normDepthTex");
  const int locTexel = GetShaderLocation(edgeShader, "texel");
  const int locNormalThreshold = GetShaderLocation(edgeShader, "normalThreshold");
  const int locDepthThreshold = GetShaderLocation(edgeShader, "depthThreshold");
  const int locEdgeIntensity = GetShaderLocation(edgeShader, "edgeIntensity");
  const int locInkColor = GetShaderLocation(edgeShader, "inkColor");

  // Static toon lighting configuration
  const Vector3 lightDirWS = Vector3Normalize({0.45f, 0.85f, 0.35f});
  const float baseCol[4] = {
      kBaseColor.r / 255.0f,
      kBaseColor.g / 255.0f,
      kBaseColor.b / 255.0f,
      1.0f};
  SetShaderValue(toonShader, locBaseColor, baseCol, SHADER_UNIFORM_VEC4);
  int toonSteps = 4;
  SetShaderValue(toonShader, locToonSteps, &toonSteps, SHADER_UNIFORM_INT);
  float ambient = 0.35f;
  SetShaderValue(toonShader, locAmbient, &ambient, SHADER_UNIFORM_FLOAT);
  float diffuseWeight = 0.75f;
  SetShaderValue(toonShader, locDiffuseWeight, &diffuseWeight, SHADER_UNIFORM_FLOAT);
  float rimWeight = 0.25f;
  SetShaderValue(toonShader, locRimWeight, &rimWeight, SHADER_UNIFORM_FLOAT);
  float specWeight = 0.12f;
  SetShaderValue(toonShader, locSpecWeight, &specWeight, SHADER_UNIFORM_FLOAT);
  float specShininess = 32.0f;
  SetShaderValue(toonShader, locSpecShininess, &specShininess, SHADER_UNIFORM_FLOAT);

  float normalThreshold = 0.25f;
  float depthThreshold = 0.002f;
  float edgeIntensity = 1.0f;
  SetShaderValue(edgeShader, locNormalThreshold, &normalThreshold, SHADER_UNIFORM_FLOAT);
  SetShaderValue(edgeShader, locDepthThreshold, &depthThreshold, SHADER_UNIFORM_FLOAT);
  SetShaderValue(edgeShader, locEdgeIntensity, &edgeIntensity, SHADER_UNIFORM_FLOAT);
  const Color outlineColor = BLACK;
  const float inkColor[4] = {
      outlineColor.r / 255.0f,
      outlineColor.g / 255.0f,
      outlineColor.b / 255.0f,
      1.0f};
  SetShaderValue(edgeShader, locInkColor, inkColor, SHADER_UNIFORM_VEC4);

  auto makeRenderTargets = [&]() {
    const int width = std::max(static_cast<int>(viewport.width), 1);
    const int height = std::max(static_cast<int>(viewport.height), 1);
    RenderTexture2D color = LoadRenderTexture(width, height);
    RenderTexture2D normDepth = LoadRenderTexture(width, height);
    return std::make_pair(color, normDepth);
  };

  auto [rtColor, rtNormalDepth] = makeRenderTargets();
  SetShaderValueTexture(edgeShader, locNormDepthTexture, rtNormalDepth.texture);
  const float initialTexel[2] = {
      1.0f / static_cast<float>(rtNormalDepth.texture.width),
      1.0f / static_cast<float>(rtNormalDepth.texture.height)};
  SetShaderValue(edgeShader, locTexel, initialTexel, SHADER_UNIFORM_VEC2);

  int prevScreenWidth = static_cast<int>(viewport.width);
  int prevScreenHeight = static_cast<int>(viewport.height);
  const float zNear = static_cast<float>(rlGetCullDistanceNear());
  const double defaultFar = rlGetCullDistanceFar();
  auto nextSceneCheck = FrameClock::now();

  while (!WindowShouldClose()) {
    const auto frameStarted = FrameClock::now();
    const bool minimized = IsWindowMinimized();
    const int targetFps = minimized ? kMinimizedFps : (IsWindowFocused() ? kFocusedFps : kBackgroundFps);
    const auto frameBudget = std::chrono::duration_cast<FrameClock::duration>(
        std::chrono::duration<double>(1.0 / targetFps));
    auto finishFrame = [&]() {
#if defined(_WIN32)
      // Custom frame control leaves event pumping to the application.
      PollInputEvents();
#else
      // Visible frames already poll in EndDrawing(); minimized frames skip it.
      if (minimized) PollInputEvents();
#endif
      // Includes rendering/reload time and never spins or catches up missed frames.
      std::this_thread::sleep_until(frameStarted + frameBudget);
    };
    const Vector2 mouseDelta = GetMouseDelta();
    bool reloadRequested = false;
    const auto input=dingcad::ReadPanelInput();

    auto reloadScene = [&]() {
      auto load = LoadSceneFromFile(runtime, scriptPath);
      if (load.success) {
        sessions[liveSceneKey]=tree.state;
        const auto key=scriptPath.string();
        tree.state=sessions.count(key)?sessions.at(key):dingcad::TreeSession{};
        scene = load.manifold;
        tree.Reload(SceneParts(scene,load.appearance));
        partModels.Reload(tree);updateBounds();liveSceneKey=key;
        exportValid=true;exportDialog.overwrite=false;
        dimensions = std::move(load.dimensions);
        reportStatus(load.message);
      } else {
        exportValid=false;exportDialog.overwrite=false;
        reportStatus(load.message+" | export disabled until corrected");
      }
      if (!load.dependencies.empty()) {
        setWatchedFiles(load.dependencies);
      }
    };

    if (!scriptPath.empty() && frameStarted >= nextSceneCheck) {
      nextSceneCheck = frameStarted + kSceneCheckInterval;
      bool changed = false;
      for (const auto &entry : watchedFiles) {
        std::error_code ec;
        auto currentTs = std::filesystem::last_write_time(entry.first, ec);
        if (ec) {
          if (entry.second.timestamp.has_value()) {
            changed = true;
            break;
          }
        } else if (!entry.second.timestamp.has_value() ||
                   currentTs != *entry.second.timestamp) {
          changed = true;
          break;
        }
      }
      if (changed) {
        reloadRequested = true;
      }
    }

    const bool modalWasOpen=exportDialog.open;
    const bool keyboardWasCaptured=modalWasOpen||panel.searchFocus;
    dingcad::PanelActions actions;
    if(!modalWasOpen)actions=panel.Update(tree,input,GetScreenWidth(),GetScreenHeight());
    viewport=panel.Viewport(GetScreenWidth(),GetScreenHeight());
    const bool captureKeyboard=keyboardWasCaptured||panel.searchFocus||exportDialog.open||
      panel.CapturesMouse(input,GetScreenWidth(),GetScreenHeight());
    const bool captureMouse=modalWasOpen||exportDialog.open||panel.CapturesMouse(input,GetScreenWidth(),GetScreenHeight());
    if(!captureKeyboard&&IsKeyPressed(KEY_R))reloadRequested=true;
    if(reloadRequested&&!scriptPath.empty())reloadScene();
    if(actions.openExport||(!captureKeyboard&&IsKeyPressed(KEY_P)))exportDialog.Open(defaultExportPath);
    if(modalWasOpen){
      const auto dialogAction=exportDialog.Update(input,GetScreenWidth(),GetScreenHeight(),tree.ExportIndices(exportDialog.visibleOnly).size(),exportValid);
      if(dialogAction.save){
        try {
          const auto savePath=std::filesystem::u8path(exportDialog.path);
          const auto result=dingcad::ExportParts(tree,exportDialog.visibleOnly,exportValid,savePath,
                                                exportDialog.overwrite,exportDialog.error);
          if(result==dingcad::ExportResult::ConfirmOverwrite)exportDialog.overwrite=true;
          else if(result==dingcad::ExportResult::Saved){
            reportStatus("Saved "+savePath.u8string());exportDialog.open=false;
          }
        }catch(const std::exception &e){exportDialog.error=e.what();}

      }
    }
    const Vector2 localMouse{input.mouse.x-viewport.x,input.mouse.y};
    dingcad::UpdateDimensionControls(dimensionControls,
      dingcad::DimensionButtonBounds(static_cast<int>(viewport.width),static_cast<int>(viewport.height)),localMouse,
      !captureKeyboard&&IsKeyPressed(KEY_M),!captureMouse&&input.pressed,input.leftDown);
    if(actions.frame)frameParts(true);
    updateBounds();

    if (!captureMouse && !captureKeyboard && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !dimensionControls.buttonGesture) {
      orbitYaw -= mouseDelta.x * 0.01f;
      orbitPitch += mouseDelta.y * 0.01f;
      const float limit = DEG2RAD * 89.0f;
      orbitPitch = Clamp(orbitPitch, -limit, limit);
    }

    const float wheel = captureMouse||captureKeyboard?0:input.wheel;
    if (wheel != 0.0f) {
      orbitDistance = dingcad::ZoomCameraDistance(orbitDistance, wheel);
    }

    const Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    const Vector3 worldUp = {0.0f, 1.0f, 0.0f};
    const Vector3 right = Vector3Normalize(Vector3CrossProduct(worldUp, forward));
    if (!captureMouse && !captureKeyboard && IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
      camera.target = Vector3Add(camera.target,
                                dingcad::PanCameraOffset(camera, mouseDelta, GetScreenHeight()));
    }

    if (!captureKeyboard && IsKeyPressed(KEY_SPACE)) frameParts(false);

    const float moveSpeed = 0.05f * orbitDistance;
    if (!captureKeyboard && IsKeyDown(KEY_W)) camera.target = Vector3Add(camera.target, Vector3Scale(forward, moveSpeed));
    if (!captureKeyboard && IsKeyDown(KEY_S)) camera.target = Vector3Add(camera.target, Vector3Scale(forward, -moveSpeed));
    if (!captureKeyboard && IsKeyDown(KEY_A)) camera.target = Vector3Add(camera.target, Vector3Scale(right, -moveSpeed));
    if (!captureKeyboard && IsKeyDown(KEY_D)) camera.target = Vector3Add(camera.target, Vector3Scale(right, moveSpeed));
    if (!captureKeyboard && IsKeyDown(KEY_Q)) camera.target = Vector3Add(camera.target, Vector3Scale(worldUp, -moveSpeed));
    if (!captureKeyboard && IsKeyDown(KEY_E)) camera.target = Vector3Add(camera.target, Vector3Scale(worldUp, moveSpeed));

    const Vector3 offsets = {
        orbitDistance * cosf(orbitPitch) * sinf(orbitYaw),
        orbitDistance * sinf(orbitPitch),
        orbitDistance * cosf(orbitPitch) * cosf(orbitYaw)};
    camera.position = Vector3Add(camera.target, offsets);
    camera.up = worldUp;

    if (minimized) {
      finishFrame();
      continue;
    }

    const int screenWidth = std::max(static_cast<int>(viewport.width), 1);
    const int screenHeight = std::max(static_cast<int>(viewport.height), 1);
    if (screenWidth != prevScreenWidth || screenHeight != prevScreenHeight) {
      UnloadRenderTexture(rtColor);
      UnloadRenderTexture(rtNormalDepth);
      auto resizedTargets = makeRenderTargets();
      rtColor = resizedTargets.first;
      rtNormalDepth = resizedTargets.second;
      SetShaderValueTexture(edgeShader, locNormDepthTexture, rtNormalDepth.texture);
      const float texel[2] = {
          1.0f / static_cast<float>(rtNormalDepth.texture.width),
          1.0f / static_cast<float>(rtNormalDepth.texture.height)};
      SetShaderValue(edgeShader, locTexel, texel, SHADER_UNIFORM_VEC2);
      prevScreenWidth = screenWidth;
      prevScreenHeight = screenHeight;
    }

    Matrix view = GetCameraMatrix(camera);
    Vector3 lightDirVS = {
        view.m0 * lightDirWS.x + view.m4 * lightDirWS.y + view.m8 * lightDirWS.z,
        view.m1 * lightDirWS.x + view.m5 * lightDirWS.y + view.m9 * lightDirWS.z,
        view.m2 * lightDirWS.x + view.m6 * lightDirWS.y + view.m10 * lightDirWS.z};
    lightDirVS = Vector3Normalize(lightDirVS);
    SetShaderValue(toonShader, locLightDirVS, &lightDirVS.x, SHADER_UNIFORM_VEC3);

    float outlineThickness = 0.0f;
    {
      const float pixels = 2.0f;
      const float distance = Vector3Distance(camera.position, camera.target);
      const float screenHeightF = static_cast<float>(screenHeight);
      const float worldPerPixel = (screenHeightF > 0.0f)
                                      ? 2.0f * tanf(DEG2RAD * camera.fovy * 0.5f) * distance / screenHeightF
                                      : 0.0f;
      outlineThickness = pixels * worldPerPixel;
    }
    setOutlineUniforms(outlineThickness, outlineColor);

    const float zFar = dingcad::CameraFarClip(camera.position, renderBounds, defaultFar);
    rlSetClipPlanes(zNear, zFar);
    SetShaderValue(normalDepthShader, locNear, &zNear, SHADER_UNIFORM_FLOAT);
    SetShaderValue(normalDepthShader, locFar, &zFar, SHADER_UNIFORM_FLOAT);

    BeginTextureMode(rtColor);
    ClearBackground({242,242,236,255});BeginMode3D(camera);
    DrawXZGrid(kGridHalfLines,kGridSpacing,Fade(LIGHTGRAY,0.18f));DrawAxes(kAxesLength);
    if(kUsePostProcessing){
      rlDisableBackfaceCulling();partModels.Draw(tree,&outlineMat);rlEnableBackfaceCulling();
      partModels.Draw(tree,&toonMat);
    }else partModels.Draw(tree);
    if(auto selected=partModels.Bounds(tree,true))DrawBoundingBox(*selected,{218,151,44,255});
    EndMode3D();EndTextureMode();
    if(kUsePostProcessing){
      BeginTextureMode(rtNormalDepth);ClearBackground({127,127,255,0});BeginMode3D(camera);
      partModels.Draw(tree,&normalDepthMat);EndMode3D();EndTextureMode();
    }
    BeginDrawing();ClearBackground({242,242,236,255});
    if(kUsePostProcessing)BeginShaderMode(edgeShader);
    DrawTextureRec(rtColor.texture,{0,0,static_cast<float>(screenWidth),-static_cast<float>(screenHeight)},
      {viewport.x,viewport.y},WHITE);
    if(kUsePostProcessing)EndShaderMode();
    // Draw overlays in viewport-local coordinates, then translate into the window.
    BeginScissorMode(static_cast<int>(viewport.x),0,screenWidth,screenHeight);
    rlPushMatrix();rlTranslatef(viewport.x,0,0);
    if(partModels.Bounds(tree))dingcad::DrawDimensions(dimensions,dimensionControls.mode,camera,brandingFont,localMouse,
      captureMouse||input.leftDown||input.rightDown||dimensionControls.overButton,screenWidth,screenHeight);
    else DrawTextEx(brandingFont,"Tutte le parti sono nascoste",{panel.Bounds(screenWidth,screenHeight).width+20,64},18,0,DARKGRAY);
    dingcad::DrawDimensionButton(dimensionControls.mode,brandingFont,screenWidth,screenHeight,localMouse);
    const auto brandSize=MeasureTextEx(brandingFont,kBrandText,kBrandFontSize,0);
    DrawTextEx(brandingFont,kBrandText,{screenWidth-brandSize.x-20,14},kBrandFontSize,0,DARKGRAY);
    if(!statusMessage.empty())DrawTextEx(brandingFont,statusMessage.c_str(),{panel.Bounds(screenWidth,screenHeight).width+12,50},14,0,exportValid?DARKGRAY:MAROON);
    rlPopMatrix();EndScissorMode();
    panel.Draw(tree,brandingFont,GetScreenWidth(),GetScreenHeight());
    exportDialog.Draw(brandingFont,GetScreenWidth(),GetScreenHeight(),tree.ExportIndices(exportDialog.visibleOnly).size(),exportValid);
    if(uiPreview&&previewFrames==2){
      rlDrawRenderBatchActive();Image shot=LoadImageFromScreen();ExportImage(shot,argv[3]);UnloadImage(shot);
    }
    EndDrawing();
#if defined(_WIN32)
    // vcpkg's raylib is built with SUPPORT_CUSTOM_FRAME_CONTROL. In that
    // configuration EndDrawing() only flushes draw commands; presenting the
    // frame is the application's job. finishFrame() pumps events in every state.
    SwapScreenBuffer();
#endif
    finishFrame();
    if(uiPreview&&++previewFrames>=3)break;
  }

  UnloadRenderTexture(rtColor);
  UnloadRenderTexture(rtNormalDepth);
  UnloadMaterial(toonMat);
  UnloadMaterial(normalDepthMat);
  UnloadMaterial(outlineMat);   // also releases the shader
  UnloadShader(edgeShader);
  partModels.Clear();
  if (brandingFontCustom) {
    UnloadFont(brandingFont);
  }
  JS_FreeRuntime(runtime);
  CloseWindow();

  return 0;
}
