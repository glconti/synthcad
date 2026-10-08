#include "display_scale.h"

#include "raylib.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <optional>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef NOGDI
#define NOGDI
#endif
#ifndef NOUSER
#define NOUSER
#endif
#include <windows.h>
#elif defined(SYNTHCAD_X11_DPI)
#define Font X11Font
#include <X11/Xlib.h>
#include <X11/Xresource.h>
#undef Font
#endif

namespace dingcad {
namespace {
constexpr double kBaseDpi = 96.0;

float RaylibUiScale() {
  const Vector2 scale = GetWindowScaleDPI();
  return UiScaleFromDpi(static_cast<double>(scale.x) * kBaseDpi);
}

#if defined(_WIN32)
float WindowsUiScale() {
  const HWND window = static_cast<HWND>(GetWindowHandle());
  if (window) {
    const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
      using GetDpiForWindowFunction = UINT(WINAPI *)(HWND);
      const auto getDpiForWindow = reinterpret_cast<GetDpiForWindowFunction>(
          GetProcAddress(user32, "GetDpiForWindow"));
      if (getDpiForWindow) {
        const UINT dpi = getDpiForWindow(window);
        if (dpi != 0) return UiScaleFromDpi(static_cast<double>(dpi));
      }
    }
  }
  return RaylibUiScale();
}
#elif defined(SYNTHCAD_X11_DPI)
std::optional<double> ParseDpiResource(const char *address, unsigned int size) {
  if (!address || size == 0) return {};
  std::string value(address, size);
  while (!value.empty() && value.back() == '\0') value.pop_back();
  char *end = nullptr;
  const double dpi = std::strtod(value.c_str(), &end);
  if (end == value.c_str()) return {};
  while (*end && std::isspace(static_cast<unsigned char>(*end))) ++end;
  if (*end != '\0' || !std::isfinite(dpi) || dpi <= 0) return {};
  return dpi;
}

std::optional<double> FindXftDpi(XrmDatabase database) {
  if (!database) return {};
  char *type = nullptr;
  XrmValue value{};
  if (!XrmGetResource(database, "Xft.dpi", "Xft.Dpi", &type, &value)) return {};
  return ParseDpiResource(value.addr, value.size);
}

std::optional<double> ReadXftDpi(Display *display) {
  if (!display) return {};
  static const bool initialized = [] {
    XrmInitialize();
    return true;
  }();
  (void)initialized;

  // Read the root RESOURCE_MANAGER property each refresh so xrdb updates take
  // effect without restarting the viewer. XGetWindowProperty lengths are in
  // 32-bit units, so 16384 bounds the fetched payload to 64 KiB.
  const Atom resourceManager = XInternAtom(display, "RESOURCE_MANAGER", True);
  if (resourceManager != None) {
    Atom actualType = None;
    int actualFormat = 0;
    unsigned long itemCount = 0;
    unsigned long bytesAfter = 0;
    unsigned char *property = nullptr;
    const int status = XGetWindowProperty(
        display, DefaultRootWindow(display), resourceManager, 0, 16384, False,
        AnyPropertyType, &actualType, &actualFormat, &itemCount, &bytesAfter,
        &property);
    (void)bytesAfter;
    if (status == Success && actualType != None && actualFormat == 8 && property) {
      const auto count = static_cast<unsigned int>(
          std::min<unsigned long>(itemCount, 64UL * 1024UL));
      XrmDatabase database = XrmGetStringDatabase(
          std::string(reinterpret_cast<const char *>(property), count).c_str());
      XFree(property);
      if (auto dpi = FindXftDpi(database)) {
        if (database) XrmDestroyDatabase(database);
        return dpi;
      }
      if (database) XrmDestroyDatabase(database);
    } else if (property) {
      XFree(property);
    }
  }

  // XOpenDisplay's cached database is a fallback for servers that do not
  // expose RESOURCE_MANAGER as a readable root property.
  return FindXftDpi(XrmGetDatabase(display));
}

float X11UiScale() {
  using Clock = std::chrono::steady_clock;
  struct Cache {
    Display *display = nullptr;
    Clock::time_point refreshed{};
    bool hasRefreshed = false;
    float scale = 0;
  };
  static Cache cache;

  const auto now = Clock::now();
  if (!cache.hasRefreshed || now - cache.refreshed >= std::chrono::seconds(1)) {
    cache.hasRefreshed = true;
    cache.refreshed = now;
    if (!cache.display) cache.display = XOpenDisplay(nullptr);
    const auto dpi = ReadXftDpi(cache.display);
    cache.scale = dpi ? UiScaleFromDpi(*dpi) : 0;
  }
  return cache.scale > 0 ? cache.scale : RaylibUiScale();
}
#endif

}  // namespace

float UiScaleFromDpi(double dpi) {
  if (!std::isfinite(dpi) || dpi <= 0) return 1.0f;
  return static_cast<float>(std::clamp(dpi / kBaseDpi, 1.0, 4.0));
}

float NativeUiScale() {
#if defined(_WIN32)
  return WindowsUiScale();
#elif defined(SYNTHCAD_X11_DPI)
  return X11UiScale();
#else
  // Wayland and other unsupported platforms use raylib's native scale when
  // available; this adapter makes no X11 DPI claim without the X11 build flag.
  return RaylibUiScale();
#endif
}

}  // namespace dingcad
