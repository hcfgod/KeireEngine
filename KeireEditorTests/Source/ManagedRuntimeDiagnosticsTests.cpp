#include "KeireClient/Editor/ManagedRuntimeDiagnostics.h"

#include <doctest/doctest.h>

#include <array>
#include <span>
#include <string>
#include <utility>
#include <vector>

TEST_CASE("Managed runtime diagnostics bridge publishes complete Console messages without polling duplicates")
{
    KeireEditor::ManagedRuntimeDiagnosticsBridge bridge;
    const Keire::AssetId entity(0x1020304050607080ULL, 0x90A0B0C0D0E0F000ULL);
    Keire::ManagedRuntimeDiagnostic fault;
    fault.Severity = Keire::ManagedDiagnosticSeverity::Error;
    fault.Callback = Keire::ManagedBehaviourCallback::Enable;
    fault.Generation = 7;
    fault.TypeName = "Gameplay.PlayerController";
    fault.Entity = entity;
    fault.Message = "Camera field of view is unavailable.";
    std::vector diagnostics{fault};

    const auto first = bridge.Collect(diagnostics);
    REQUIRE(first.size() == 1);
    CHECK(first.front().Severity == Keire::ManagedDiagnosticSeverity::Error);
    CHECK(first.front().Generation == 7);
    CHECK(first.front().Message.find("Type: Gameplay.PlayerController") != std::string::npos);
    CHECK(first.front().Message.find("Callback: OnEnable") != std::string::npos);
    CHECK(first.front().Message.find("Entity: " + entity.ToString()) != std::string::npos);
    CHECK(first.front().Message.find("Generation: 7") != std::string::npos);
    CHECK(first.front().Message.find("Message: Camera field of view is unavailable.") != std::string::npos);
    CHECK(bridge.ConsumedCount() == 1);

    CHECK(bridge.Collect(diagnostics).empty());
    CHECK(bridge.ConsumedCount() == 1);

    diagnostics.push_back(fault);
    const auto retriedFault = bridge.Collect(diagnostics);
    REQUIRE(retriedFault.size() == 1);
    CHECK(retriedFault.front().Message == first.front().Message);
    CHECK(bridge.ConsumedCount() == 2);

    const auto truncatedSource = bridge.Collect(std::span(diagnostics).first(1));
    REQUIRE(truncatedSource.size() == 1);
    CHECK(truncatedSource.front().Message == first.front().Message);
    CHECK(bridge.ConsumedCount() == 1);
}

TEST_CASE("Managed runtime diagnostics bridge distinguishes generations and resets for a new project lifecycle")
{
    KeireEditor::ManagedRuntimeDiagnosticsBridge bridge;
    Keire::ManagedRuntimeDiagnostic generationOne;
    generationOne.Severity = Keire::ManagedDiagnosticSeverity::Warning;
    generationOne.Callback = Keire::ManagedBehaviourCallback::FixedUpdate;
    generationOne.Generation = 1;
    generationOne.TypeName = "Gameplay.Motor";
    generationOne.Entity = Keire::AssetId(1, 2);
    generationOne.Message = "simulation warning";

    REQUIRE(bridge.Collect(std::array{generationOne}).size() == 1);

    auto generationTwo = generationOne;
    generationTwo.Generation = 2;
    const std::array afterReload{generationOne, generationTwo};
    const auto reloaded = bridge.Collect(afterReload);
    REQUIRE(reloaded.size() == 1);
    CHECK(reloaded.front().Generation == 2);
    CHECK(reloaded.front().Severity == Keire::ManagedDiagnosticSeverity::Warning);
    CHECK(bridge.Collect(afterReload).empty());

    bridge.Reset();
    CHECK(bridge.ConsumedCount() == 0);
    const auto newProject = bridge.Collect(std::array{generationTwo});
    REQUIRE(newProject.size() == 1);
    CHECK(newProject.front().Generation == 2);
}

TEST_CASE("Managed runtime diagnostics bridge formats missing and unknown diagnostic context safely")
{
    KeireEditor::ManagedRuntimeDiagnosticsBridge bridge;
    Keire::ManagedRuntimeDiagnostic diagnostic;
    diagnostic.Callback = static_cast<Keire::ManagedBehaviourCallback>(255);
    diagnostic.Generation = 3;

    const auto entries = bridge.Collect(std::array{diagnostic});
    REQUIRE(entries.size() == 1);
    CHECK(entries.front().Message.find("Type: <unknown>") != std::string::npos);
    CHECK(entries.front().Message.find("Callback: Unknown") != std::string::npos);
    CHECK(entries.front().Message.find("Entity: <none>") != std::string::npos);
    CHECK(entries.front().Message.find("Message: <no diagnostic message>") != std::string::npos);
}

TEST_CASE("Managed runtime diagnostics bridge names every managed callback")
{
    using Callback = Keire::ManagedBehaviourCallback;
    const std::array expected{
        std::pair{Callback::Awake, "Awake"},
        std::pair{Callback::Enable, "OnEnable"},
        std::pair{Callback::Start, "Start"},
        std::pair{Callback::FixedUpdate, "FixedUpdate"},
        std::pair{Callback::Update, "Update"},
        std::pair{Callback::LateUpdate, "LateUpdate"},
        std::pair{Callback::AnimationEvent, "OnAnimationEvent"},
        std::pair{Callback::PhysicsContact, "Physics Contact"},
        std::pair{Callback::Disable, "OnDisable"},
        std::pair{Callback::Destroy, "OnDestroy"},
        std::pair{Callback::BeforeReload, "OnBeforeReload"},
        std::pair{Callback::AfterReload, "OnAfterReload"},
        std::pair{Callback::AnimatorIk, "OnAnimatorIk"},
        std::pair{Callback::ProceduralMotionEvent, "OnProceduralMotionEvent"},
    };

    for (const auto& [callback, name] : expected)
        CHECK(KeireEditor::ManagedBehaviourCallbackDisplayName(callback) == name);
}
