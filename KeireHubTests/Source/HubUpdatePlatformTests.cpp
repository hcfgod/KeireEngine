#include "KeireHub/HubUpdatePlatform.h"

#include <doctest/doctest.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <array>
#include <filesystem>
#include <string_view>

TEST_CASE("Native Windows Hub update trust recognizes an unsigned executable")
{
    std::array<wchar_t, 32'768> executablePath{};
    const auto pathLength =
        GetModuleFileNameW(nullptr, executablePath.data(), static_cast<DWORD>(executablePath.size()));
    REQUIRE(pathLength > 0);
    REQUIRE(static_cast<std::size_t>(pathLength) < executablePath.size());

    CHECK(KeireHub::NativeHubUpdatePlatformSignaturePolicy() ==
          KeireHub::HubUpdatePlatformSignaturePolicy::ValidateIfPresent);
    const auto signature = KeireHub::VerifyNativeHubInstallerSignature(
        std::filesystem::path(std::wstring_view(executablePath.data(), pathLength)));
    REQUIRE(signature);
    CHECK(signature.Value() == KeireHub::HubUpdatePlatformSignatureState::NotPresent);
}
#endif
