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
#include <iomanip>
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
#include "brand.h"
#include "stl_export.h"
#include "agent_entry.h"
#include "agent_cli.h"
#include "agent_transport.h"
#include "agent_bridge.h"
#include "agent_scene.h"
#include "design_graph.h"
#include "geometry_picker.h"
#include "selection_reference.h"
#include "selection_ui.h"
#include "guided_pick_ui.h"
#include "display_scale.h"

namespace {
const Color kBaseColor = {210, 210, 220, 255};

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
  synthcad::FileSnapshot files;
};

ModuleLoaderData g_module_loader_data;

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
  return synthcad::ReadTrackedFile(path,g_module_loader_data.files);
}

char* FilesystemModuleNormalize(JSContext* ctx,const char* base,const char* name,void*){
  try {
    auto path=std::filesystem::u8path(name);
    if(path.is_relative())path=std::filesystem::u8path(base).parent_path()/path;
    const auto normalized=synthcad::CanonicalPath(path);
    return js_strdup(ctx,normalized.c_str());
  }catch(const std::exception& error){JS_ThrowReferenceError(ctx,"%s",error.what());return nullptr;}
}

JSModuleDef *FilesystemModuleLoader(JSContext *ctx, const char *module_name, void *opaque) {
  auto *data = static_cast<ModuleLoaderData *>(opaque);
  std::filesystem::path resolved=std::filesystem::u8path(module_name);
  if (resolved.is_relative()) {
    const std::filesystem::path base = data && !data->baseDir.empty()
                                           ? data->baseDir
                                           : std::filesystem::current_path();
    resolved = base / resolved;
  }
  resolved = std::filesystem::absolute(resolved).lexically_normal();

  if (data) {
    data->dependencies.insert(resolved);
  }

  auto source = ReadTextFile(resolved);
  if (!source) {
    JS_ThrowReferenceError(ctx, "Unable to load module '%s'", resolved.u8string().c_str());
    return nullptr;
  }

  const std::string moduleName = resolved.u8string();
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
  nlohmann::json design;
  synthcad::FileSnapshot files;
};

LoadResult LoadSceneFromFile(JSRuntime *runtime, const std::filesystem::path &path,
                             const std::string& requestedView = "") {
  LoadResult result;
  const auto loadStarted=std::chrono::steady_clock::now();
  const auto absolutePath = std::filesystem::absolute(path);
  g_module_loader_data.baseDir = absolutePath.parent_path();
  g_module_loader_data.dependencies.clear();
  g_module_loader_data.files.clear();
  g_module_loader_data.dependencies.insert(absolutePath);
  auto sourceOpt = ReadTextFile(absolutePath);
  if (!sourceOpt) {
    result.message = "Unable to read scene file: " + absolutePath.u8string();
    result.dependencies.assign(g_module_loader_data.dependencies.begin(),
                               g_module_loader_data.dependencies.end());
    result.files=g_module_loader_data.files;
    return result;
  }
  JSContext *ctx = JS_NewContext(runtime);
  RegisterBindings(ctx);

  auto captureException = [&]() {
    JSValue exc = JS_GetException(ctx);
    JSValue stack = JS_GetPropertyStr(ctx, exc, "stack");
    const char *exceptionStr=JS_ToCString(ctx,exc);
    const char *stackStr=JS_IsUndefined(stack)?nullptr:JS_ToCString(ctx,stack);
    result.message=exceptionStr?exceptionStr:"JavaScript error";
    if(stackStr)result.message+=std::string("\n")+stackStr;
    JS_FreeCString(ctx,exceptionStr);
    JS_FreeCString(ctx, stackStr);
    JS_FreeValue(ctx, stack);
    JS_FreeValue(ctx, exc);
  };
  auto assignDependencies = [&]() {
    result.dependencies.assign(g_module_loader_data.dependencies.begin(),
                               g_module_loader_data.dependencies.end());
    result.files=g_module_loader_data.files;
  };

  JSValue moduleFunc = JS_Eval(ctx, sourceOpt->c_str(), sourceOpt->size(), absolutePath.u8string().c_str(),
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
  // QuickJS module evaluation returns a promise, including synchronous throws.
  // Preserve its rejection before touching uninitialized namespace exports.
  if(JS_PromiseState(ctx,evalResult)==JS_PROMISE_REJECTED){
    JS_Throw(ctx,JS_PromiseResult(ctx,evalResult));
    captureException();assignDependencies();
    JS_FreeValue(ctx,evalResult);JS_FreeContext(ctx);return result;
  }
  JS_FreeValue(ctx, evalResult);

  JSValue moduleNamespace = JS_GetModuleNamespace(ctx, module);
  if (JS_IsException(moduleNamespace)) {
    captureException();
    assignDependencies();
    JS_FreeContext(ctx);
    return result;
  }

  auto design = dingcad::ReadDesignGraph(ctx, moduleNamespace, requestedView);
  if (!design.diagnostic.empty()) {
    result.message = design.diagnostic;
    JS_FreeValue(ctx, moduleNamespace);assignDependencies();JS_FreeContext(ctx);
    return result;
  }
  JSValue sceneVal = design.specified ? JS_UNDEFINED : JS_GetPropertyStr(ctx, moduleNamespace, "scene");
  if (JS_IsException(sceneVal)) {
    JS_FreeValue(ctx, moduleNamespace);
    captureException();
    assignDependencies();
    JS_FreeContext(ctx);
    return result;
  }
  auto annotations = dingcad::ReadDimensions(ctx, moduleNamespace);
  result.appearance = design.specified ? std::move(design.appearance) : dingcad::ReadAppearance(ctx, moduleNamespace);
  if (design.specified) result.design = std::move(design.metadata);
  JS_FreeValue(ctx, moduleNamespace);

  if(!result.appearance.diagnostic.empty()){
    result.message=result.appearance.diagnostic;
    JS_FreeValue(ctx,sceneVal);JS_FreeContext(ctx);assignDependencies();return result;
  }

  if (!design.specified && JS_IsUndefined(sceneVal)) {
    JS_FreeValue(ctx, sceneVal);
    JS_FreeContext(ctx);
    result.message = "Scene module must export 'scene'";
    assignDependencies();
    return result;
  }

  auto sceneHandle = design.specified ? design.scene : GetManifoldHandle(ctx, sceneVal);
  if (!sceneHandle) {
    JS_FreeValue(ctx, sceneVal);
    JS_FreeContext(ctx);
    result.message = "Exported 'scene' is not a manifold";
    assignDependencies();
    return result;
  }

  result.manifold = sceneHandle;
  result.success = true;
  result.message = "Loaded " + absolutePath.u8string();
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
  return {{scene,kBaseColor,"@scene","Scene",{},true}};
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
  auto utf8Args=synthcad::ProcessArguments(argc,argv);
  if(synthcad::IsAgentCommand(utf8Args))return synthcad::RunAgentCli(
    std::vector<std::string>(utf8Args.begin()+1,utf8Args.end()),utf8Args.front());
  std::vector<char*> utf8Pointers;for(auto& arg:utf8Args)utf8Pointers.push_back(arg.data());
  argc=static_cast<int>(utf8Pointers.size());argv=utf8Pointers.data();
  std::string agentSession,agentProject;
  bool agentHidden=false;
  if(argc>1&&std::string(argv[1])=="--agent-session"){
    for(int i=1;i<argc;++i){const std::string flag=argv[i];
      if(flag=="--agent-session"&&i+1<argc)agentSession=argv[++i];
      else if(flag=="--agent-project"&&i+1<argc)agentProject=argv[++i];
      else if(flag=="--agent-hidden")agentHidden=true;
      else {std::cerr<<"Invalid agent viewer option\n";return 2;}
    }
    if(agentSession.empty()||agentProject.empty()){std::cerr<<"Agent session and project are required\n";return 2;}
  }
  std::optional<synthcad::Project> project;
  std::string activeView;
  std::unique_ptr<synthcad::AgentBridge> agent;
  synthcad::SessionServer agentServer;
  struct SessionCleanup {
    std::unique_ptr<synthcad::AgentBridge>& bridge;synthcad::SessionServer& server;
    ~SessionCleanup(){if(bridge)bridge->Close();server.Stop();}
  } sessionCleanup{agent,agentServer};
  if(!agentSession.empty()){
    try{project=synthcad::LoadProject(std::filesystem::u8path(agentProject));activeView=project->defaultView;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}
    agent=std::make_unique<synthcad::AgentBridge>(agentSession);
    std::string error;
    if(!agentServer.Start(agentSession,project->path.u8string(),[&](const nlohmann::json& request){return agent->Handle(request);},error)){
      std::cerr<<error<<'\n';return 10;
    }
  }
  // Deterministic preview using the same mesh colors, creases and default
  // material as the interactive Windows view. Never exports or changes solids.
  if (argc==4 && std::string(argv[1])=="--render-scene") {
    JSRuntime *runtime=JS_NewRuntime();EnsureManifoldClass(runtime);
    JS_SetModuleLoaderFunc(runtime,FilesystemModuleNormalize,FilesystemModuleLoader,&g_module_loader_data);
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
    JS_SetModuleLoaderFunc(runtime,FilesystemModuleNormalize,FilesystemModuleLoader,&g_module_loader_data);
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
    JS_SetModuleLoaderFunc(runtime, FilesystemModuleNormalize, FilesystemModuleLoader, &g_module_loader_data);
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
  SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_ALWAYS_RUN | ((uiPreview||agentHidden)?FLAG_WINDOW_HIDDEN:0));
  dingcad::SetApplicationIdentity();
  InitWindow(1280, 720, "SynthCAD");
  dingcad::SetApplicationIcons();
  const std::string previewMode=uiPreview&&argc>4?argv[4]:"";
  auto hasPreview=[&](const char *m){return previewMode.find(m)!=std::string::npos;};
  const float forcedScale=hasPreview("scale200")?2.f:hasPreview("scale150")?1.5f:0.f;
  const int previewFrameLimit=hasPreview("watch")?120:3;
  float uiScale=forcedScale?forcedScale:dingcad::NativeUiScale();
  if(uiPreview)SetWindowSize(int((hasPreview("small")?720:1280)*uiScale),int((hasPreview("small")?480:720)*uiScale));
  SetWindowMinSize(int(640*uiScale),int(400*uiScale));
  if(!uiPreview)SetWindowSize(int(1280*uiScale),int(720*uiScale));
  // Pace frames ourselves: this raylib build's WaitTime() spins a CPU core.
  SetTargetFPS(0);
  SetExitKey(KEY_NULL);

  Font brandingFont = GetFontDefault();
  bool brandingFontCustom = false;
#if defined(_WIN32)
  const std::filesystem::path uiFontPath("C:/Windows/Fonts/segoeui.ttf");
#elif defined(__APPLE__)
  const std::filesystem::path uiFontPath("/System/Library/Fonts/Supplemental/Arial.ttf");
#else
  const std::filesystem::path uiFontPath=[](){
    for(const auto* path:{"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                          "/usr/share/fonts/TTF/DejaVuSans.ttf"})
      if(std::filesystem::exists(path))return std::filesystem::path(path);
    return std::filesystem::path{};
  }();
#endif
  auto loadUiFont=[&](){
    if(brandingFontCustom)UnloadFont(brandingFont);
    brandingFont=GetFontDefault();brandingFontCustom=false;
    if(std::filesystem::exists(uiFontPath)){
      std::vector<int> glyphs;for(int c=32;c<384;++c)glyphs.push_back(c);
      glyphs.push_back(0x2014);glyphs.push_back(0x2026);
      brandingFont=LoadFontEx(uiFontPath.u8string().c_str(),int(32*uiScale),glyphs.data(),int(glyphs.size()));
      SetTextureFilter(brandingFont.texture,TEXTURE_FILTER_BILINEAR);brandingFontCustom=true;
    }
  };
  loadUiFont();

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
  JS_SetModuleLoaderFunc(runtime, FilesystemModuleNormalize, FilesystemModuleLoader, &g_module_loader_data);

  std::shared_ptr<manifold::Manifold> scene = nullptr;
  std::vector<dingcad::Dimension> dimensions;
  dingcad::Appearance appearance;
  dingcad::DimensionControls dimensionControls;
  std::string statusMessage;
  dingcad::WorkspaceUi workspace;
  bool exportValid=false;
  std::filesystem::path scriptPath;
  synthcad::FileSnapshot watchedFiles,attemptedFiles,displayedFiles;
  std::string attemptedRevision,displayedRevision,loadStatus="loading",loadDiagnostic;
  std::string displayedSourceRevision,displayedView;
  nlohmann::json displayedDesign;
  std::vector<std::string> agentHighlights;
  std::optional<std::filesystem::path> defaultScript;
  if(project){defaultScript=synthcad::ResolveView(*project,activeView);}
  else if (argc > 1) {
    defaultScript = std::filesystem::u8path(argv[uiPreview?2:1]);
    if(!uiPreview){try{project=synthcad::LoadProject(*defaultScript);activeView=project->defaultView;defaultScript=synthcad::ResolveView(*project,activeView);}catch(const std::exception&) {}}
  } else {
    defaultScript = FindDefaultScene();
  }
  auto reportStatus = [&](const std::string &message) {
    statusMessage = message;
    TraceLog(LOG_INFO, "%s", statusMessage.c_str());
    std::cout << statusMessage << std::endl;
  };
  auto recordLoad = [&](LoadResult& load) {
    if(project&&!project->standalone)load.files.insert(project->files.begin(),project->files.end());
    attemptedFiles=load.files;attemptedRevision=synthcad::Revision(attemptedFiles);
    if(load.success&&!synthcad::MatchesDisk(attemptedFiles)){
      load.success=false;load.message="Source changed during evaluation; waiting for a stable revision.";loadStatus="loading";
    }else loadStatus=load.success?"ready":"failed";
    loadDiagnostic=load.success?"":load.message;
    // Preserve old dependency keys after failure without recapturing consumed
    // file digests. Newly edited bytes must still trigger another attempt.
    if(load.success)watchedFiles=attemptedFiles;
    else {
      for(auto& f:watchedFiles)if(!attemptedFiles.count(f.first)){
        synthcad::FileSnapshot current;synthcad::ReadTrackedFile(std::filesystem::u8path(f.first),current);f.second=current.begin()->second;
      }
      for(const auto& f:attemptedFiles)watchedFiles[f.first]=f.second;
    }
    if(load.success){
      displayedFiles=attemptedFiles;displayedSourceRevision=attemptedRevision;
      displayedView=activeView;displayedDesign=load.design;
      // Same source bytes can describe several layouts. Guards identify what
      // was displayed, including the active view and resolved graph placement.
      const auto identity=load.design.is_object()?load.design.value("identity",""):"";
      displayedRevision=synthcad::Sha256("synthcad-display-v1:"+
          std::to_string(activeView.size())+":"+activeView+attemptedRevision+identity);
    }
  };
  if (defaultScript) {
    scriptPath = std::filesystem::absolute(*defaultScript);
    if(agent){
      auto initial=synthcad::CaptureFiles({scriptPath,project->path});
      agent->Publish({{"status","loading"},{"view",activeView},{"projectPath",project->path.u8string()},
        {"displayedRevision",""},{"attemptedRevision",""},{"exportValid",false},{"selection",nullptr}},initial);
    }
    auto load = LoadSceneFromFile(runtime, scriptPath, project&&!project->standalone?activeView:"");
    recordLoad(load);
    if (load.success) {
      scene = load.manifold;
      exportValid=true;
      dimensions = std::move(load.dimensions);
      appearance = std::move(load.appearance);
      reportStatus(load.message);
      TraceLog(LOG_INFO,"Scene load: %.1f ms (model evaluation %.1f ms)",load.loadMilliseconds,load.evaluationMilliseconds);
    } else {
      workspace.Failed(load.message);
      reportStatus(load.message);
    }
  }
  if (!scene) {
    manifold::Manifold cube = manifold::Manifold::Cube({2.0, 2.0, 2.0}, true);
    manifold::Manifold sphere = manifold::Manifold::Sphere(1.2, 0);
    manifold::Manifold combo = cube + sphere.Translate({0.0, 0.8, 0.0});
    scene = std::make_shared<manifold::Manifold>(defaultScript?manifold::Manifold{}:combo);
    if(defaultScript)appearance.specified=true;
    if (!defaultScript) exportValid=true;
    if (statusMessage.empty()) {
      reportStatus("No scene.js found. Using built-in sample.");
    }
  }

  dingcad::PartTree tree;
  tree.Reload(SceneParts(scene,appearance));
  appearance.parts.clear();
  PartModels partModels;partModels.Reload(tree);
  if(displayedRevision.empty()&&!defaultScript)displayedRevision="builtin-v1";
  dingcad::selection::GeometryPicker picker;
  picker.Reload(tree,displayedRevision);
  dingcad::SelectionUi selectionUi;
  dingcad::GuidedPickUi guidedUi;
  std::string guidedId;
  dingcad::SelectionMode previousPickMode=selectionUi.mode;
  nlohmann::json guidedRequest=nullptr,guidedCandidate=nullptr;
  auto syncGuidedPick=[&](){
    auto current=agent?agent->ActivePick():nlohmann::json(nullptr);
    const auto id=current.is_null()?std::string{}:current.value("id","");
    if(id!=guidedId){
      if(!guidedId.empty())selectionUi.mode=previousPickMode;
      guidedCandidate=nullptr;guidedUi.scroll=0;guidedUi.gesture=false;
      if(!id.empty()){
        previousPickMode=selectionUi.mode;
        const auto kind=current.value("kind","");
        selectionUi.mode=kind=="surface"?dingcad::SelectionMode::Surface:
          kind=="edge"?dingcad::SelectionMode::Edge:kind=="vertex"?dingcad::SelectionMode::Vertex:dingcad::SelectionMode::Part;
        guidedUi.question=current.value("question","");guidedUi.kind=kind;
      }
      guidedId=id;
    }
    guidedRequest=std::move(current);guidedUi.active=!guidedId.empty();
  };
  auto validGuidedCandidate=[&](){
    if(guidedRequest.is_null()||guidedCandidate.is_null()||loadStatus!="ready"||
       guidedRequest.value("revision","")!=displayedRevision)return false;
    const auto kind=guidedCandidate.value("kind","");
    if(guidedUi.kind=="surface"?(kind!="planar-face"&&kind!="curved-patch"):kind!=guidedUi.kind)return false;
    if(!guidedCandidate.contains("reference")||!guidedCandidate["reference"].is_string())return false;
    auto ref=dingcad::selection::DecodeReference(guidedCandidate["reference"].get<std::string>());
    if(!ref||ref->revision!=displayedRevision)return false;
    auto p=std::find_if(tree.parts.begin(),tree.parts.end(),[&](const auto& part){return part.id==ref->partId;});
    if(p==tree.parts.end()||!tree.Visible(size_t(p-tree.parts.begin())))return false;
    if(ref->geometry){
      const auto* topology=picker.Get(size_t(p-tree.parts.begin()));
      return topology&&dingcad::selection::ResolveReference(*ref,*topology).has_value();
    }
    return true;
  };
  dingcad::PickGesture pickGesture;
  std::optional<dingcad::selection::Hit> geometryHit;
  nlohmann::json selectionGeometry=nullptr;
  auto clearGeometry=[&](){geometryHit.reset();selectionGeometry=nullptr;};
  auto selectHit=[&](const std::optional<dingcad::selection::Hit>& hit){
    clearGeometry();tree.state.selected.clear();
    if(!hit)return;
    const auto& part=tree.parts.at(hit->partIndex);
    tree.state.selected="p:"+part.id;
    geometryHit=hit;
    selectionGeometry=hit->feature?dingcad::selection::GeometryJson(*hit->feature,tree):
      dingcad::selection::PartGeometryJson(part.id,displayedRevision,tree);
  };
  dingcad::PartsPanel panel;
  dingcad::ExportDialog exportDialog;
  panel.scenePath=scriptPath.u8string();
  if(!scriptPath.empty())panel.sceneName=scriptPath.filename().u8string();
  SetWindowTitle((std::string("SynthCAD \xE2\x80\x94 ")+panel.sceneName).c_str());
  std::unordered_map<std::string,dingcad::TreeSession> sessions;
  std::string liveSceneKey=scriptPath.u8string()+"\nview:"+activeView;
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
  auto publishAgent=[&](){if(!agent)return;
    auto snapshot=synthcad::ReviewSnapshot(tree,dimensions,camera,agentHighlights);
    if(!snapshot["selection"].is_null()){
      if(!selectionGeometry.is_null())snapshot["selection"]["geometry"]=selectionGeometry;
      else if(!snapshot["selection"]["group"].get<bool>()&&!displayedRevision.empty())
        snapshot["selection"]["geometry"]=dingcad::selection::PartGeometryJson(
          snapshot["selection"]["partIds"][0].get<std::string>(),displayedRevision,tree);
    }
    snapshot["selectionDiagnostics"]=picker.Diagnostics();
    snapshot["status"]=loadStatus;snapshot["attemptedRevision"]=attemptedRevision;
    snapshot["displayedRevision"]=displayedRevision;snapshot["exportValid"]=exportValid;
    snapshot["sourceRevision"]=displayedSourceRevision;snapshot["displayedView"]=displayedView;
    snapshot["design"]=displayedDesign;
    snapshot["diagnostic"]=loadDiagnostic;snapshot["view"]=activeView;
    snapshot["projectPath"]=project?project->path.u8string():scriptPath.u8string();
    snapshot["views"]=nlohmann::json::object();
    if(project)for(const auto& view:project->views)snapshot["views"][view.first]=view.second.u8string();
    snapshot["dependencies"]=nlohmann::json::array();for(const auto& f:watchedFiles)snapshot["dependencies"].push_back(f.first);
    agent->Publish(std::move(snapshot),loadStatus=="ready"?displayedFiles:attemptedFiles);
  };
  publishAgent();
  auto defaultExportPath=dingcad::SuggestedExportPath(GetHomeDirectory().value_or(std::filesystem::current_path()),scriptPath).u8string();
  std::vector<std::shared_ptr<synthcad::AgentAction>> pendingScreenshots;
  if(uiPreview&&argc>4){
    const std::string mode=argv[4];
    if(mode=="closed")panel.open=false;

    if(mode=="selected"||mode=="isolated"){
      for(size_t n=0;n<tree.nodes.size();++n)if(tree.nodes[n].name=="Oggetto progettato")tree.Select(n);
      if(mode=="isolated")tree.Isolate();
    }
    if(mode=="dimensions")dimensionControls.mode=dingcad::DimensionMode::All;
    if(hasPreview("export")){exportDialog.Open(defaultExportPath);if(hasPreview("edit")){exportDialog.pathFocus=true;exportDialog.editor.Focus(exportDialog.path,true);}}
    if(mode=="hidden")for(auto &[_,f]:tree.state.flags)f.visible=false;
    if(hasPreview("empty")){tree.Reload({});partModels.Reload(tree);}
    if(hasPreview("error")){workspace.Failed("Model evaluation failed\nError: displayParts[2]: duplicate id 'saddle-left'\nCheck the component IDs in assembly.js.\nLa geometria precedente resta visibile.\n\n"+scriptPath.u8string());workspace.details=true;exportValid=false;}
    if(hasPreview("toast"))workspace.Saved("assembly.stl",GetTime());
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
    dingcad::UnloadApplicationIcons();
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
#if defined(SYNTHCAD_CUSTOM_FRAME_CONTROL)
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
    const float currentScale=forcedScale?forcedScale:dingcad::NativeUiScale();
    if(currentScale!=uiScale){uiScale=currentScale;loadUiFont();SetWindowMinSize(int(640*uiScale),int(400*uiScale));}
    const int uiWidth=int(GetScreenWidth()/uiScale),uiHeight=int(GetScreenHeight()/uiScale);
    dingcad::SetUiDrawScale(uiScale);
    const auto input=dingcad::LogicalInput(dingcad::ReadPanelInput(),uiScale);

    auto reloadScene = [&]() {
      loadStatus="loading";exportValid=false;publishAgent();
      LoadResult load;
      try {
        if(project){auto next=synthcad::LoadProject(project->path);
          auto nextPath=synthcad::ResolveView(next,activeView);project=std::move(next);scriptPath=nextPath;}
        load=LoadSceneFromFile(runtime,scriptPath,project&&!project->standalone?activeView:"");
      }catch(const std::exception& error){load.message=error.what();
        if(project)synthcad::ReadTrackedFile(project->path,load.files);
      }
      recordLoad(load);
      if (load.success) {
        sessions[liveSceneKey]=tree.state;
        const auto key=scriptPath.u8string()+"\nview:"+activeView;
        tree.state=sessions.count(key)?sessions.at(key):dingcad::TreeSession{};
        scene = load.manifold;
        tree.Reload(SceneParts(scene,load.appearance));
        clearGeometry();guidedCandidate=nullptr;picker.Reload(tree,displayedRevision);
        partModels.Reload(tree);updateBounds();liveSceneKey=key;
        exportValid=true;exportDialog.overwrite=false;workspace.Loaded();
        dimensions = std::move(load.dimensions);
        agentHighlights.erase(std::remove_if(agentHighlights.begin(),agentHighlights.end(),[&](const auto& id){
          return std::none_of(tree.parts.begin(),tree.parts.end(),[&](const auto& part){return part.id==id;});}),agentHighlights.end());
        panel.scenePath=scriptPath.u8string();panel.sceneName=scriptPath.filename().u8string();
        SetWindowTitle((std::string("SynthCAD \xE2\x80\x94 ")+panel.sceneName).c_str());
        defaultExportPath=dingcad::SuggestedExportPath(GetHomeDirectory().value_or(std::filesystem::current_path()),scriptPath).u8string();
        reportStatus(load.message);
      } else {
        exportValid=false;exportDialog.overwrite=false;workspace.Failed(load.message);
        reportStatus(load.message+" | export disabled until corrected");
      }
      publishAgent();
    };

    if (!scriptPath.empty() && frameStarted >= nextSceneCheck) {
      nextSceneCheck = frameStarted + kSceneCheckInterval;
      const bool changed = !synthcad::MatchesDisk(watchedFiles);
      if (changed) {
        reloadRequested = true;
      }
    }

    if(agent)for(auto& action:agent->Drain()){
      const auto& request=action->request;
      const std::string command=request.value("command","");
      const auto args=request.value("arguments",nlohmann::json::object());
      auto fail=[&](const std::string& code,const std::string& message){action->result.set_value(synthcad::Error(command,code,message,{},agentSession,displayedRevision));};
      if(std::chrono::steady_clock::now()>=action->deadline){fail("timeout","Request expired before processing");continue;}
      if(exportDialog.open){fail("busy","Close the export dialog before changing the review view");continue;}
      const auto guard=request.value("expectRevision","");
      if(!guard.empty()&&(guard!=displayedRevision||loadStatus!="ready"||!synthcad::MatchesDisk(displayedFiles))){fail("stale_revision","Displayed revision is no longer current");continue;}
      try{
        if(command=="pick"){
          auto result=agent->BeginPick(args,displayedRevision);
          action->result.set_value(synthcad::Success(command,result,agentSession,result.at("request").value("revision","")));
          continue;
        }
        if(command=="view"){
          if(!project){fail("not_found","This scene has no named project views");continue;}
          const auto name=args.at("name").get<std::string>();
          synthcad::ResolveView(*project,name);activeView=name;agentHighlights.clear();reloadScene();
          if(loadStatus!="ready"){fail("load_failed",loadDiagnostic);continue;}
          action->result.set_value(synthcad::Success(command,{{"view",activeView}},agentSession,displayedRevision));continue;
        }
        if(command=="screenshot"){
          if(minimized){fail("busy","Restore the viewer before capturing a screenshot");continue;}
          auto path=std::filesystem::u8path(args.at("path").get<std::string>());
          if(path.extension()!=".png"){fail("invalid_argument","Screenshot destination must end in .png");continue;}
          if(std::filesystem::exists(path)&&!args.value("replace",false)){fail("io_error","Destination exists; use --replace to overwrite");continue;}
          pendingScreenshots.push_back(action);continue;
        }
        if(command=="reference"){
          const auto ref=dingcad::selection::DecodeReference(args.at("reference").get<std::string>());
          if(!ref){fail("invalid_argument","Malformed selection reference");continue;}
          if(ref->revision!=displayedRevision||loadStatus!="ready"||!synthcad::MatchesDisk(displayedFiles)){
            fail("stale_revision","Selection reference is not from the current valid displayed revision");continue;
          }
          auto it=std::find_if(tree.parts.begin(),tree.parts.end(),[&](const auto& p){return p.id==ref->partId;});
          if(it==tree.parts.end()){fail("not_found","Reference owner is not in this view");continue;}
          nlohmann::json context;
          if(ref->geometry){
            const auto* topology=picker.Get(size_t(it-tree.parts.begin()));
            const auto resolved=topology?dingcad::selection::ResolveReference(*ref,*topology):std::nullopt;
            if(!resolved){fail("stale_revision","Geometric reference no longer matches the displayed topology");continue;}
            context=dingcad::selection::GeometryJson(*resolved,tree,true);
          }else context=dingcad::selection::PartGeometryJson(ref->partId,displayedRevision,tree);
          action->result.set_value(synthcad::Success(command,{{"geometry",context}},agentSession,displayedRevision));continue;
        }
        if(command!="highlight"&&command!="frame"){fail("invalid_argument","Unknown review command");continue;}
        const auto ids=args.value("partIds",std::vector<std::string>{});
        const bool clear=args.value("clear",false);
        auto indices=clear?std::vector<size_t>{}:synthcad::ResolveReviewParts(tree,ids,args.value("selection",false));
        if(command=="highlight"){
          agentHighlights.clear();for(auto i:indices)agentHighlights.push_back(tree.parts[i].id);
        }
        if(command=="frame"||args.value("frame",false)){
          std::optional<BoundingBox> bounds;
          for(auto i:indices)if(tree.Visible(i)&&partModels.models[i].meshCount){
            const auto& b=partModels.bounds[i];
            if(bounds){bounds->min=Vector3Min(bounds->min,b.min);bounds->max=Vector3Max(bounds->max,b.max);}else bounds=b;
          }
          if(bounds){camera=FrameScene(*bounds,static_cast<int>(viewport.width),static_cast<int>(viewport.height));
            orbitDistance=Vector3Distance(camera.position,camera.target);
            orbitYaw=atan2f(camera.position.x-camera.target.x,camera.position.z-camera.target.z);
            orbitPitch=asinf((camera.position.y-camera.target.y)/orbitDistance);}
        }
        publishAgent();
        action->result.set_value(synthcad::Success(command,{{"highlights",agentHighlights}},agentSession,displayedRevision));
      }catch(const synthcad::GuidedPickError& error){fail(error.code,error.what());}
      catch(const std::exception& error){fail("not_found",error.what());}
    }

    syncGuidedPick();
    guidedUi.canConfirm=validGuidedCandidate();
    const bool modalWasOpen=exportDialog.open;
    const bool keyboardWasCaptured=modalWasOpen||panel.searchFocus;
    dingcad::PanelActions actions;
    dingcad::PanelActions workspaceActions;
    dingcad::SelectionActions selectionActions;
    dingcad::GuidedPickActions guidedActions;
    const bool guidedUiActive=guidedUi.active&&workspace.loadError.empty()&&!workspace.help;
    workspace.toastBottom=guidedUiActive?guidedUi.Bounds(uiWidth,uiHeight).height+24:128;
    const auto guidedCaptures=[&](){return guidedUiActive&&guidedUi.CapturesMouse(input,uiWidth,uiHeight);};
    const auto showSelectionUi=[&](){return !guidedUi.active&&workspace.loadError.empty()&&!workspace.help;};
    const bool selectionUiActive=showSelectionUi();
    if(!selectionUiActive)selectionUi.gesture=false;
    const auto selectionCaptures=[&](){return selectionUiActive&&selectionUi.CapturesMouse(input,uiWidth,uiHeight);};
    if(!modalWasOpen){
      if(guidedUiActive){auto guidedInput=input;
        if(keyboardWasCaptured)guidedInput.escape=guidedInput.enter=false;
        guidedActions=guidedUi.Update(guidedInput,uiWidth,uiHeight,[&](const std::string& text){return MeasureTextEx(brandingFont,text.c_str(),16,0).x;});
      }
      if(selectionUiActive)selectionActions=selectionUi.Update(input,uiWidth,uiHeight);
      if(!selectionCaptures()&&!guidedCaptures())workspaceActions=workspace.Update(input,uiWidth,uiHeight,GetTime());
      if((!workspace.CapturesMouse(input,uiWidth,uiHeight,GetTime())&&!selectionCaptures()&&!guidedCaptures())||input.find)
        actions=panel.Update(tree,input,uiWidth,uiHeight);
    }
    if(actions.selectionChanged)clearGeometry();
    viewport=panel.Viewport(GetScreenWidth(),GetScreenHeight());
    const bool captureKeyboard=keyboardWasCaptured||panel.searchFocus||exportDialog.open||
      (panel.CapturesMouse(tree,input,uiWidth,uiHeight)||workspace.CapturesMouse(input,uiWidth,uiHeight,GetTime())||selectionCaptures()||guidedCaptures());
    const bool captureMouse=modalWasOpen||exportDialog.open||(panel.CapturesMouse(tree,input,uiWidth,uiHeight)||workspace.CapturesMouse(input,uiWidth,uiHeight,GetTime())||selectionCaptures()||guidedCaptures());
    if(workspaceActions.reload||(!captureKeyboard&&IsKeyPressed(KEY_R)))reloadRequested=true;
    if(reloadRequested&&!scriptPath.empty())reloadScene();
    if(actions.openExport||(!captureKeyboard&&IsKeyPressed(KEY_P)))exportDialog.Open(defaultExportPath);
    if(modalWasOpen){
      const auto dialogAction=exportDialog.Update(input,uiWidth,uiHeight,tree.ExportIndices(exportDialog.visibleOnly).size(),exportValid);
      if(dialogAction.save){
        try {
          const auto savePath=std::filesystem::u8path(exportDialog.path);
          const auto result=dingcad::ExportParts(tree,exportDialog.visibleOnly,exportValid,savePath,
                                                exportDialog.overwrite,exportDialog.error);
          if(result==dingcad::ExportResult::ConfirmOverwrite)exportDialog.overwrite=true;
          else if(result==dingcad::ExportResult::Saved){
            reportStatus("Saved "+savePath.u8string());workspace.Saved(savePath.filename().u8string(),GetTime());exportDialog.open=false;
          }
        }catch(const std::exception &e){exportDialog.error=e.what();}

      }
    }
    const Vector2 localMouse=input.mouse;
    if(workspaceActions.dimensions||(!captureKeyboard&&IsKeyPressed(KEY_M)))
      dimensionControls.mode=dingcad::NextDimensionMode(dimensionControls.mode);
    if(workspaceActions.fitAll)frameParts(false);
    if(actions.frame)frameParts(true);
    if(!guidedUi.active&&!captureKeyboard&&IsKeyPressed(KEY_ESCAPE))agentHighlights.clear();
    updateBounds();

    const auto click=pickGesture.Update(input.mouse,input.pressed,input.leftDown,IsMouseButtonReleased(MOUSE_BUTTON_LEFT),
      captureMouse||captureKeyboard||input.rightDown||dimensionControls.buttonGesture);
    if(click)selectHit(picker.PickAt(tree,camera,*click,uiWidth,uiHeight,
      static_cast<dingcad::selection::ReviewMode>(selectionUi.mode)));
    if(selectionActions.clear){clearGeometry();tree.state.selected.clear();}
    nlohmann::json copyGeometry=selectionGeometry;
    if(copyGeometry.is_null())if(auto n=tree.Selection())if(!tree.nodes[*n].group&&!displayedRevision.empty())
      copyGeometry=dingcad::selection::PartGeometryJson(tree.parts[tree.nodes[*n].parts[0]].id,displayedRevision,tree);
    if(guidedUi.active&&(click||actions.selectionChanged))guidedCandidate=copyGeometry;
    guidedUi.canConfirm=validGuidedCandidate();
    if(agent&&guidedUi.active){
      if(guidedActions.cancel)agent->Handle({{"command","pick-cancel"},{"arguments",{{"id",guidedId}}}});
      else if(guidedActions.confirm&&guidedUi.canConfirm){
        if(!synthcad::MatchesDisk(displayedFiles))agent->InvalidatePick("source_changed");
        else agent->ConfirmPick(guidedId,guidedCandidate,displayedRevision);
      }
      syncGuidedPick();
    }
    selectionUi.hasSelection=!copyGeometry.is_null();
    selectionUi.copyAvailable=selectionUi.hasSelection&&copyGeometry["reference"].is_string();
    selectionUi.summary="Click a part to select";selectionUi.detail="Click selects; drag orbits";
    if(selectionUi.hasSelection){
      const auto kind=copyGeometry.value("kind","");
      const std::string label=kind=="planar-face"?"Planar face":kind=="curved-patch"?"Curved patch":kind=="edge"?"Edge":kind=="vertex"?"Vertex":"Part";
      const auto id=copyGeometry.value("partId","");
      const auto p=std::find_if(tree.parts.begin(),tree.parts.end(),[&](const auto& part){return part.id==id;});
      selectionUi.summary=label+" · "+(p!=tree.parts.end()?p->name:id);
      selectionUi.detail="Reference belongs to this revision";
      if(copyGeometry["position"].is_array()){
        const auto p=copyGeometry["position"].get<std::array<double,3>>();std::ostringstream detail;
        detail<<std::fixed<<std::setprecision(2)<<(kind=="part"?"Center ":"XYZ ")<<p[0]<<", "<<p[1]<<", "<<p[2]<<" mm";
        selectionUi.detail=detail.str();
      }
      if(copyGeometry.contains("referenceError"))selectionUi.detail=copyGeometry["referenceError"].get<std::string>();
    }else if(tree.Selection())selectionUi.summary="Group selected in parts tree";
    else if(!picker.Diagnostics().empty())selectionUi.detail="Some features unavailable; use Part mode";
    if((selectionActions.copy||(!captureKeyboard&&IsKeyDown(KEY_LEFT_CONTROL)&&IsKeyPressed(KEY_C)))&&selectionUi.hasSelection&&selectionUi.copyAvailable){
      const auto reference=copyGeometry.at("reference").get<std::string>();SetClipboardText(reference.c_str());
      const auto* copied=GetClipboardText();
      workspace.toast=copied&&reference==copied?"Selection reference copied":"Clipboard unavailable; use synthcad selection";
      workspace.toastUntil=GetTime()+4;
    }
    if (!captureMouse && !captureKeyboard && pickGesture.Dragging() && !dimensionControls.buttonGesture) {
      orbitYaw -= mouseDelta.x / uiScale * 0.01f;
      orbitPitch += mouseDelta.y / uiScale * 0.01f;
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
    publishAgent();

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
    for(size_t i=0;i<tree.parts.size();++i)if(tree.Visible(i)&&
      std::find(agentHighlights.begin(),agentHighlights.end(),tree.parts[i].id)!=agentHighlights.end())
      DrawBoundingBox(partModels.bounds[i],{48,136,192,255});
    if(geometryHit&&geometryHit->feature&&tree.Visible(geometryHit->partIndex)){
      const auto* topology=picker.Get(geometryHit->partIndex);
      if(topology&&topology->Contains(geometryHit->feature->reference)){
        const auto& feature=topology->Features()[geometryHit->feature->reference.id];
        auto world=[&](uint32_t id){const auto& p=topology->Points()[id];return dingcad::CadToWorld({float(p.x),float(p.y),float(p.z)});};
        const Color ink={226,151,33,255};
        if(feature.kind==dingcad::selection::Kind::Edge){
          for(size_t n=1;n<feature.members.size();++n)DrawLine3D(world(feature.members[n-1]),world(feature.members[n]),ink);
        }else if(feature.kind==dingcad::selection::Kind::Vertex){
          DrawSphere(world(feature.members[0]),std::max(.003f,orbitDistance*.003f),ink);
        }else{
          rlDisableBackfaceCulling();
          for(auto t:feature.members){const auto& tri=topology->Triangles()[t];
            auto a=world(tri[0]),b=world(tri[1]),c=world(tri[2]);
            const auto offset=Vector3Scale(Vector3Normalize(Vector3CrossProduct(Vector3Subtract(b,a),Vector3Subtract(c,a))),std::max(.0001f,orbitDistance*.00002f));
            DrawTriangle3D(Vector3Add(a,offset),Vector3Add(b,offset),Vector3Add(c,offset),Fade(ink,.45f));
          }rlEnableBackfaceCulling();
        }
      }
    }
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
    // UI uses logical pixels; 3D projection and render targets stay full-window.
    rlPushMatrix();rlScalef(uiScale,uiScale,1);
    if(partModels.Bounds(tree))dingcad::DrawDimensions(dimensions,dimensionControls.mode,camera,brandingFont,localMouse,
      captureMouse||input.leftDown||input.rightDown,uiWidth,uiHeight);
    else {
      const char *empty=tree.parts.empty()?"This scene has no parts":"All parts are hidden. Use Show all to restore them.";
      const float textWidth=MeasureTextEx(brandingFont,empty,18,0).x;
      DrawTextEx(brandingFont,empty,{std::max(12.f,(uiWidth-textWidth)/2),uiHeight-80.f},18,0,DARKGRAY);
    }
    panel.Draw(tree,brandingFont,uiWidth,uiHeight);
    workspace.Draw(brandingFont,uiWidth,uiHeight,dimensionControls.mode,GetTime());
    if(selectionUiActive&&showSelectionUi())selectionUi.Draw(brandingFont,uiWidth,uiHeight);
    if(guidedUi.active&&workspace.loadError.empty()&&!workspace.help)guidedUi.Draw(brandingFont,uiWidth,uiHeight,uiScale);
    exportDialog.Draw(brandingFont,uiWidth,uiHeight,tree.ExportIndices(exportDialog.visibleOnly).size(),exportValid);
    if(!agentHighlights.empty())DrawTextEx(brandingFont,guidedUi.active?"Agent highlight":"Agent highlight - Esc to clear",{380.f,62.f},16,0,{48,106,142,255});
    rlPopMatrix();
    for(auto& action:pendingScreenshots){
      const auto args=action->request.at("arguments");const auto path=args.at("path").get<std::string>();
      if(std::chrono::steady_clock::now()>=action->deadline){
        action->result.set_value(synthcad::Error("screenshot","timeout","Screenshot request expired",{},agentSession,displayedRevision));continue;
      }
      const auto guard=action->request.value("expectRevision","");
      if(!guard.empty()&&(guard!=displayedRevision||loadStatus!="ready"||!synthcad::MatchesDisk(displayedFiles))){
        action->result.set_value(synthcad::Error("screenshot","stale_revision","Displayed revision changed before capture",{},agentSession,displayedRevision));continue;
      }
      try{
        rlDrawRenderBatchActive();Image shot=LoadImageFromScreen();int size=0;
        auto* bytes=ExportImageToMemory(shot,".png",&size);UnloadImage(shot);
        if(!bytes||size<=0){if(bytes)MemFree(bytes);throw std::runtime_error("Screenshot encoding failed");}
        std::string encoded(reinterpret_cast<char*>(bytes),size);MemFree(bytes);
        synthcad::WriteOutputFile(std::filesystem::u8path(path),encoded,args.value("replace",false));
        action->result.set_value(synthcad::Success("screenshot",{{"path",path}},agentSession,displayedRevision));
      }catch(const std::exception& error){action->result.set_value(synthcad::Error("screenshot","io_error",error.what(),{},agentSession,displayedRevision));}
    }
    pendingScreenshots.clear();
    if(uiPreview&&previewFrames==previewFrameLimit-1){
      TraceLog(LOG_INFO,"UI QA: %zu parts, export %s, error %s",tree.parts.size(),exportValid?"enabled":"disabled",workspace.loadError.empty()?"clear":"present");
      rlDrawRenderBatchActive();Image shot=LoadImageFromScreen();ExportImage(shot,argv[3]);UnloadImage(shot);
    }
    EndDrawing();
#if defined(SYNTHCAD_CUSTOM_FRAME_CONTROL)
    // The linked raylib uses SUPPORT_CUSTOM_FRAME_CONTROL. In that
    // configuration EndDrawing() only flushes draw commands; presenting the
    // frame is the application's job. finishFrame() pumps events in every state.
    SwapScreenBuffer();
#endif
    finishFrame();
    if(uiPreview&&++previewFrames>=previewFrameLimit)break;
  }

  if(agent){agent->Close();agentServer.Stop();}
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
  dingcad::UnloadApplicationIcons();
  CloseWindow();

  return 0;
}
