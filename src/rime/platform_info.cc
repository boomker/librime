#include <rime/platform_info.h>

#include <algorithm>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <sys/utsname.h>
#endif

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace rime {

namespace {

string Normalize(string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return value;
}

bool IsArmArchitecture(const string& architecture) {
  const auto arch = Normalize(architecture);
  return arch == "arm64" || arch == "aarch64" || arch == "arm";
}

string DetectOsFamily() {
#if defined(__ANDROID__)
  return "android";
#elif defined(__APPLE__) && TARGET_OS_IPHONE
  return "ios";
#elif defined(__APPLE__) && TARGET_OS_OSX
  return "macos";
#elif defined(_WIN32)
  return "windows";
#elif defined(__linux__)
  return "linux";
#elif defined(__FreeBSD__)
  return "freebsd";
#else
  return "unknown";
#endif
}

string DetectRuntimeArchitecture() {
#if defined(_WIN32)
  SYSTEM_INFO system_info;
  GetNativeSystemInfo(&system_info);
  switch (system_info.wProcessorArchitecture) {
    case PROCESSOR_ARCHITECTURE_AMD64:
      return "x86_64";
    case PROCESSOR_ARCHITECTURE_ARM64:
      return "arm64";
    case PROCESSOR_ARCHITECTURE_INTEL:
      return "x86";
    case PROCESSOR_ARCHITECTURE_ARM:
      return "arm";
    default:
      return "unknown";
  }
#elif defined(__unix__) || defined(__APPLE__)
  struct utsname system_info;
  if (uname(&system_info) == 0) {
    return Normalize(system_info.machine);
  }
  return "unknown";
#else
  return "unknown";
#endif
}

}  // namespace

DeviceClass InferDeviceClass(const string& os_family,
                             const string& architecture,
                             const string& distribution_code_name) {
  const auto code_name = Normalize(distribution_code_name);
  if (code_name == "trime" || code_name == "hamster") {
    return DeviceClass::kMobile;
  }
  if (code_name == "squirrel" || code_name == "weasel") {
    return DeviceClass::kDesktop;
  }

  const auto os = Normalize(os_family);
  if (os == "android" || os == "ios") {
    return DeviceClass::kMobile;
  }
  if (os == "macos" || os == "windows" || os == "linux" || os == "freebsd") {
    return DeviceClass::kDesktop;
  }

  if (IsArmArchitecture(architecture)) {
    return DeviceClass::kMobile;
  }
  if (!architecture.empty() && Normalize(architecture) != "unknown") {
    return DeviceClass::kDesktop;
  }

  return DeviceClass::kUnknown;
}

PlatformInfo GetPlatformInfo(const string& distribution_code_name) {
  PlatformInfo platform;
  platform.os_family = DetectOsFamily();
  platform.architecture = DetectRuntimeArchitecture();
  platform.device_class = InferDeviceClass(
      platform.os_family, platform.architecture, distribution_code_name);
  return platform;
}

}  // namespace rime
