#ifndef RIME_PLATFORM_INFO_H_
#define RIME_PLATFORM_INFO_H_

#include <rime_api.h>
#include <rime/common.h>

namespace rime {

enum class DeviceClass {
  kUnknown,
  kDesktop,
  kMobile,
};

struct PlatformInfo {
  string os_family;
  string architecture;
  DeviceClass device_class = DeviceClass::kUnknown;
};

RIME_DLL DeviceClass InferDeviceClass(const string& os_family,
                                      const string& architecture,
                                      const string& distribution_code_name);

RIME_DLL PlatformInfo
GetPlatformInfo(const string& distribution_code_name = string());

}  // namespace rime

#endif  // RIME_PLATFORM_INFO_H_
