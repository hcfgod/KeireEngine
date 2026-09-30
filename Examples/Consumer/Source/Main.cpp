#include "Keire/Core.h"

#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace
{
    class ConsumerSession final : public Keire::RefCounted
    {
      public:
        explicit ConsumerSession(std::string version) : Version(std::move(version)) {}

        std::string Version;
    };

    struct ConsumerEvent
    {
        int Value = 0;
    };

    bool ValidateGroundingContact()
    {
        const Keire::SkeletonAsset skeleton({{"Pelvis", -1, {{0, 2, 0}, {}, {1, 1, 1}}, {}},
                                             {"Hip", 0, {{0, 0, 0}, {}, {1, 1, 1}}, {}},
                                             {"Knee", 1, {{0, -1, 0}, {}, {1, 1, 1}}, {}},
                                             {"Foot", 2, {{0, -1, 0}, {}, {1, 1, 1}}, {}}});
        std::vector<Keire::BoneTransform> pose;
        for (const auto& bone : skeleton.Bones())
            pose.push_back(bone.BindPose);
        Keire::FootGroundingRequest request;
        request.Pelvis = 0;
        request.FootHeight = 0;
        request.Contacts.push_back({1, 2, 3, {0, -0.1F, 0}});
        request.Contacts.front().SupportPosition = Keire::Vector3{0, -0.2F, 0};
        request.Contacts.front().SupportWeight = 0.5F;
        const auto result = Keire::SolveFootGrounding(skeleton, pose, request);
        return result && result->SolvedFeet == 1 && std::abs(result->PelvisAdjustment + 0.1F) < 0.0001F &&
               result->MaximumPositionError <= request.PositionTolerance;
    }
} // namespace

int main(const int argc, char* argv[])
{
    if (argc != 2)
        return 2;
#if defined(_WIN32)
    _putenv_s("SDL_VIDEODRIVER", "dummy");
#else
    setenv("SDL_VIDEODRIVER", "dummy", 1);
#endif
    const auto& build = Keire::GetBuildInfo();
    Keire::UiSpecification uiSpecification;
    uiSpecification.Mode = Keire::UiMode::Headless;
    Keire::LogConfig config;
    config.EnableConsole = false;
    config.LogDirectory = "Logs";
    Keire::Log::Initialize(config);
    auto session = Keire::CreateRef<ConsumerSession>(std::string(build.Version));
    auto events = Keire::CreateRef<Keire::EventBus>();
    int eventValue = 0;
    auto subscription = events->Subscribe<ConsumerEvent>(
        [&eventValue](const ConsumerEvent& event)
        {
            eventValue = event.Value;
            return Keire::EventFlow::Continue;
        });
    (void)subscription;
    (void)events->Dispatch(ConsumerEvent{42});
    Keire::Time time;
    time.AdvanceFrame(Keire::TimeStep::FromMilliseconds(16.0));
    const std::string sceneSource =
        R"({"schemaVersion":4,"name":"SDK Layer Smoke","entities":[{"id":"11111111-1111-4111-8111-111111111111","parent":null,"name":"Layered","active":true,"layer":7,"components":[]}],"prefabInstances":[],"prefabOverrides":[]})";
    const std::vector<std::byte> sceneBytes(
        reinterpret_cast<const std::byte*>(sceneSource.data()),
        reinterpret_cast<const std::byte*>(sceneSource.data() + sceneSource.size()));
    const auto scene = Keire::SceneAsset::Decode(sceneBytes);
    const auto specification = Keire::LoadWindowSpecification(argv[1]);
    auto windowSystem = Keire::CreateRef<Keire::WindowSystem>();
    auto window = windowSystem->CreateWindow(specification);
    KEIRE_CORE_INFO("SDK consumer initialized with Core {}", session->Version);
    window->Close();
    window.Reset();
    windowSystem->Shutdown();
    windowSystem.Reset();
    events->Close();
    events.Reset();
    Keire::Log::Shutdown();
    return std::string(Keire::GetName()).empty() || build.Version.empty() || eventValue != 42 ||
                   time.FrameCount() != 1 || uiSpecification.Mode != Keire::UiMode::Headless || !scene ||
                   scene->Definition().SchemaVersion != Keire::CurrentSceneSchemaVersion ||
                   scene->Definition().Objects.size() != 1 || scene->Definition().Objects.front().Layer != 7 ||
                   !ValidateGroundingContact()
               ? 1
               : 0;
}
