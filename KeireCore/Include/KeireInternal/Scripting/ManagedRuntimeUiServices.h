#pragma once

#include "Keire/Scripting/ScriptSystem.h"

#include <any>

namespace Keire
{
    class ScenePresentationRuntime;
    class SceneRuntimeSession;
} // namespace Keire

namespace Keire::Detail
{
    [[nodiscard]] std::optional<ManagedUiDocumentElement>
    ManagedUiDocumentRoot(const Ref<ScenePresentationRuntime>& presentation, AssetId document) noexcept;
    [[nodiscard]] std::optional<ManagedUiDocumentElement>
    FindManagedUiDocumentElement(const Ref<ScenePresentationRuntime>& presentation, AssetId document,
                                 AssetId stableId) noexcept;
    [[nodiscard]] std::optional<ManagedUiDocumentElement>
    FindManagedUiDocumentElement(const Ref<ScenePresentationRuntime>& presentation, AssetId document,
                                 std::string_view name) noexcept;
    [[nodiscard]] bool ManagedUiDocumentElementAlive(const Ref<ScenePresentationRuntime>& presentation,
                                                     AssetId document, std::uint64_t documentGeneration,
                                                     std::uint64_t element) noexcept;
    [[nodiscard]] std::optional<std::string>
    ReadManagedUiDocumentElementText(const Ref<ScenePresentationRuntime>& presentation, AssetId document,
                                     std::uint64_t documentGeneration, std::uint64_t element) noexcept;
    [[nodiscard]] bool SetManagedUiDocumentElementText(const Ref<ScenePresentationRuntime>& presentation,
                                                       AssetId document, std::uint64_t documentGeneration,
                                                       std::uint64_t element, std::string_view text) noexcept;
    [[nodiscard]] std::optional<float>
    ReadManagedUiDocumentElementValue(const Ref<ScenePresentationRuntime>& presentation, AssetId document,
                                      std::uint64_t documentGeneration, std::uint64_t element) noexcept;
    [[nodiscard]] bool SetManagedUiDocumentElementValue(const Ref<ScenePresentationRuntime>& presentation,
                                                        AssetId document, std::uint64_t documentGeneration,
                                                        std::uint64_t element, float value) noexcept;
    [[nodiscard]] std::optional<bool>
    ReadManagedUiDocumentElementFlag(const Ref<ScenePresentationRuntime>& presentation, AssetId document,
                                     std::uint64_t documentGeneration, std::uint64_t element,
                                     ManagedUiDocumentFlag property) noexcept;
    [[nodiscard]] bool SetManagedUiDocumentElementFlag(const Ref<ScenePresentationRuntime>& presentation,
                                                       AssetId document, std::uint64_t documentGeneration,
                                                       std::uint64_t element, ManagedUiDocumentFlag property,
                                                       bool value) noexcept;
    [[nodiscard]] bool ConsumeManagedUiDocumentElementEvent(const Ref<ScenePresentationRuntime>& presentation,
                                                            AssetId document, std::uint64_t documentGeneration,
                                                            std::uint64_t element, RuntimeUiEventType type) noexcept;
    [[nodiscard]] bool FocusManagedUiDocumentElement(const Ref<ScenePresentationRuntime>& presentation,
                                                     AssetId document, std::uint64_t documentGeneration,
                                                     std::uint64_t element) noexcept;
    [[nodiscard]] bool SetManagedUiDocumentBindingValue(const Ref<SceneRuntimeSession>& session, AssetId document,
                                                        std::string_view path, std::any value) noexcept;
    [[nodiscard]] std::optional<std::any> ReadManagedUiDocumentBindingValue(const Ref<SceneRuntimeSession>& session,
                                                                            AssetId document,
                                                                            std::string_view path) noexcept;
    [[nodiscard]] bool ClearManagedUiDocumentBindingSource(const Ref<SceneRuntimeSession>& session,
                                                           AssetId document) noexcept;
} // namespace Keire::Detail
