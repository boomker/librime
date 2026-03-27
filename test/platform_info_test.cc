#include <gtest/gtest.h>
#include <rime/platform_info.h>

using namespace rime;

TEST(RimePlatformInfoTest, DetectsDesktopByDistributionCodeName) {
  EXPECT_EQ(DeviceClass::kDesktop,
            InferDeviceClass("android", "arm64", "Squirrel"));
  EXPECT_EQ(DeviceClass::kDesktop,
            InferDeviceClass("ios", "arm64", "Weasel"));
}

TEST(RimePlatformInfoTest, DetectsMobileByDistributionCodeName) {
  EXPECT_EQ(DeviceClass::kMobile,
            InferDeviceClass("macos", "x86_64", "Trime"));
  EXPECT_EQ(DeviceClass::kMobile,
            InferDeviceClass("windows", "x86_64", "Hamster"));
}

TEST(RimePlatformInfoTest, DetectsByOsFamilyWhenDistributionUnknown) {
  EXPECT_EQ(DeviceClass::kMobile,
            InferDeviceClass("android", "arm64", ""));
  EXPECT_EQ(DeviceClass::kMobile,
            InferDeviceClass("ios", "arm64", ""));
  EXPECT_EQ(DeviceClass::kDesktop,
            InferDeviceClass("macos", "arm64", ""));
  EXPECT_EQ(DeviceClass::kDesktop,
            InferDeviceClass("windows", "x86_64", ""));
}

TEST(RimePlatformInfoTest, UsesArchitectureAsFallbackHint) {
  EXPECT_EQ(DeviceClass::kMobile,
            InferDeviceClass("unknown", "arm64", ""));
  EXPECT_EQ(DeviceClass::kDesktop,
            InferDeviceClass("unknown", "x86_64", ""));
}
