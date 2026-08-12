#include "KeireHubRuntime/HubActivation.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

using namespace KeireHub;

namespace
{
    [[nodiscard]] HubActivationRequest RoundTrip(const HubActivationRequest& request)
    {
        auto encoded = EncodeHubActivation(request);
        if (!encoded || encoded.Value().size() > MaximumHubActivationFrameBytes)
            throw std::runtime_error("Could not encode the activation test fixture.");
        auto decoded = DecodeHubActivation(encoded.Value());
        if (!decoded)
            throw std::runtime_error("Could not decode the activation test fixture.");
        return std::move(decoded).Value();
    }

    [[nodiscard]] HubResult<HubActivationRequest> Parse(const std::initializer_list<std::string_view> arguments)
    {
        return ParseHubActivationArguments(std::span(arguments.begin(), arguments.size()));
    }

    [[nodiscard]] std::string PathUtf8(const std::filesystem::path& path)
    {
        const auto value = path.generic_u8string();
        return std::string(reinterpret_cast<const char*>(value.data()), value.size());
    }
} // namespace

TEST_CASE("Hub activation protocol round-trips every typed product action")
{
    const auto project = std::filesystem::current_path() / "Projects" / "IPC Fixture";
    const auto package = std::filesystem::current_path() / "Packages" / "editor.keirepackage";

    const auto show = RoundTrip({});
    CHECK(show.Action == HubActivationAction::Show);

    const auto navigate = RoundTrip({.Action = HubActivationAction::Navigate, .Page = HubPage::Licenses});
    CHECK(navigate.Action == HubActivationAction::Navigate);
    REQUIRE(navigate.Page);
    CHECK(*navigate.Page == HubPage::Licenses);

    const auto open = RoundTrip({.Action = HubActivationAction::OpenProject, .Path = project});
    CHECK(open.Action == HubActivationAction::OpenProject);
    REQUIRE(open.Path);
    CHECK(*open.Path == project);

    const auto import = RoundTrip({.Action = HubActivationAction::ImportPackage, .Path = package});
    CHECK(import.Action == HubActivationAction::ImportPackage);
    REQUIRE(import.Path);
    CHECK(*import.Path == package);

    const auto install =
        RoundTrip({.Action = HubActivationAction::InstallVersion, .VersionId = "keire.editor.stable@1.2.3"});
    CHECK(install.Action == HubActivationAction::InstallVersion);
    CHECK(install.VersionId == "keire.editor.stable@1.2.3");

    const auto support =
        RoundTrip({.Action = HubActivationAction::BuildSupport, .Platform = "windows", .Architecture = "x86_64"});
    CHECK(support.RequestsBuildSupport());
    CHECK(support.Platform == "windows");
    CHECK(support.Architecture == "x86_64");

    const auto oauth = RoundTrip(
        {.Action = HubActivationAction::OAuthCallback,
         .Url =
             "keirehub://oauth/callback?code=single-use-code-value&state=abcdefghijklmnopqrstuvwxyz0123456789_ABCD"});
    CHECK(oauth.Action == HubActivationAction::OAuthCallback);
    CHECK(oauth.Url ==
          "keirehub://oauth/callback?code=single-use-code-value&state=abcdefghijklmnopqrstuvwxyz0123456789_ABCD");
}

TEST_CASE("Hub activation command parsing is singular and strict")
{
    CHECK(Parse({}).Value().Action == HubActivationAction::Show);
    CHECK(Parse({"--show"}).Value().Action == HubActivationAction::Show);

    auto navigate = Parse({"--navigate", "templates"});
    REQUIRE(navigate);
    CHECK(navigate.Value().Page == HubPage::Templates);

    const auto project = PathUtf8(std::filesystem::current_path() / "Project");
    auto open = Parse({"--open-project", project});
    REQUIRE(open);
    CHECK(open.Value().Action == HubActivationAction::OpenProject);

    const auto package = PathUtf8(std::filesystem::current_path() / "offline.keirepackage");
    auto import = Parse({"--import-package", package});
    REQUIRE(import);
    CHECK(import.Value().Action == HubActivationAction::ImportPackage);
    auto locate = Parse({"--locate-package", package});
    REQUIRE(locate);
    CHECK(locate.Value().Action == HubActivationAction::ImportPackage);

    CHECK(Parse({"--install-version", "keire.editor.preview@2.0.0"}));
    CHECK(Parse({"--build-support", "macos", "arm64"}));
    auto oauth =
        Parse({"keirehub://oauth/callback?code=single-use-code-value&state=abcdefghijklmnopqrstuvwxyz0123456789_ABCD"});
    REQUIRE(oauth);
    CHECK(oauth.Value().Action == HubActivationAction::OAuthCallback);

    CHECK_FALSE(Parse({"--navigate"}));
    CHECK_FALSE(Parse({"--navigate", "account"}));
    CHECK_FALSE(Parse({"--open-project", "relative/project"}));
    CHECK_FALSE(Parse({"--install-version", "version with spaces"}));
    CHECK_FALSE(Parse({"--build-support", "android", "arm64"}));
    CHECK_FALSE(Parse({"--show", "--navigate", "home"}));
    CHECK_FALSE(Parse({"--unknown"}));
    CHECK_FALSE(Parse({"keirehub://oauth/callback?code=value#fragment"}));
    const std::string oversizedArgument(MaximumHubActivationFrameBytes + 1, 'a');
    CHECK_FALSE(Parse({"--install-version", oversizedArgument}));
}

TEST_CASE("Hub activation protocol rejects malformed and hostile frames")
{
    auto encoded =
        EncodeHubActivation({.Action = HubActivationAction::InstallVersion, .VersionId = "keire.editor.stable@1.2.3"});
    REQUIRE(encoded);
    const auto& valid = encoded.Value();

    CHECK_FALSE(DecodeHubActivation("show"));
    const auto oversizedFrame = std::string(MaximumHubActivationFrameBytes + 1, 'x');
    CHECK_FALSE(DecodeHubActivation(oversizedFrame));
    for (std::size_t length = 0; length < valid.size(); ++length)
        CHECK_FALSE(DecodeHubActivation(std::string_view(valid).substr(0, length)));

    auto malformed = valid;
    malformed[0] = 'X';
    CHECK_FALSE(DecodeHubActivation(malformed));
    malformed = valid;
    malformed[4] = 2;
    CHECK_FALSE(DecodeHubActivation(malformed));
    malformed = valid;
    malformed[5] = static_cast<char>(255);
    CHECK_FALSE(DecodeHubActivation(malformed));
    malformed = valid;
    malformed[8] = 2;
    CHECK_FALSE(DecodeHubActivation(malformed));
    malformed = valid;
    malformed[9] = 1;
    CHECK_FALSE(DecodeHubActivation(malformed));
    malformed = valid;
    malformed.back() = static_cast<char>(0xff);
    CHECK_FALSE(DecodeHubActivation(malformed));
    malformed = valid;
    malformed.back() = '\n';
    CHECK_FALSE(DecodeHubActivation(malformed));

    auto unsafePath = EncodeHubActivation(
        {.Action = HubActivationAction::OpenProject, .Path = std::filesystem::current_path() / "xx" / "project"});
    REQUIRE(unsafePath);
    malformed = unsafePath.Value();
    const auto traversalField = malformed.rfind("/xx/");
    REQUIRE(traversalField != std::string::npos);
    malformed[traversalField + 1] = '.';
    malformed[traversalField + 2] = '.';
    CHECK_FALSE(DecodeHubActivation(malformed));

    const auto root = std::filesystem::current_path().root_path();
    const auto traversal = root / "safe" / ".." / "escape";
    CHECK_FALSE(EncodeHubActivation({.Action = HubActivationAction::OpenProject, .Path = traversal}));
    CHECK_FALSE(EncodeHubActivation(
        {.Action = HubActivationAction::Navigate, .Page = HubPage::Home, .VersionId = "unexpected"}));

    auto oversizedPath = std::filesystem::current_path();
    oversizedPath /= std::string(MaximumHubActivationFrameBytes, 'a');
    CHECK_FALSE(EncodeHubActivation({.Action = HubActivationAction::ImportPackage, .Path = oversizedPath}));
}

TEST_CASE("Hub activation decoder rejects forged lengths and trailing bytes")
{
    auto encoded = EncodeHubActivation({.Action = HubActivationAction::Navigate, .Page = HubPage::Projects});
    REQUIRE(encoded);

    auto forged = encoded.Value();
    forged[6] = 0;
    forged[7] = 10;
    CHECK_FALSE(DecodeHubActivation(forged));

    forged = encoded.Value();
    forged[10] = 0x01;
    forged[11] = static_cast<char>(0xff);
    CHECK_FALSE(DecodeHubActivation(forged));

    forged = encoded.Value();
    forged.push_back('x');
    const auto size = forged.size();
    forged[6] = static_cast<char>((size >> 8U) & 0xffU);
    forged[7] = static_cast<char>(size & 0xffU);
    CHECK_FALSE(DecodeHubActivation(forged));
}
