#include "KeireInternal/Scripting/ManagedRuntimeUiServices.h"

#include "Keire/Scenes/ScenePresentationRuntime.h"

namespace Keire::Detail
{
    namespace
    {
        [[nodiscard]] ManagedUiDocumentElement Convert(const ScenePresentationUiDocumentElement& element) noexcept
        {
            return {.DocumentGeneration = element.DocumentGeneration,
                    .Element = element.Element,
                    .StableIdHigh = element.StableId.High(),
                    .StableIdLow = element.StableId.Low(),
                    .Type = static_cast<ManagedUiDocumentElementType>(element.Type)};
        }

        [[nodiscard]] ScenePresentationUiDocumentFlag Convert(const ManagedUiDocumentFlag property) noexcept
        {
            return static_cast<ScenePresentationUiDocumentFlag>(property);
        }
    } // namespace

    std::optional<ManagedUiDocumentElement> ManagedUiDocumentRoot(const Ref<ScenePresentationRuntime>& presentation,
                                                                  const AssetId document) noexcept
    {
        try
        {
            const auto element = presentation ? presentation->UiDocumentRoot(EntityId(document)) : std::nullopt;
            return element ? std::optional(Convert(*element)) : std::nullopt;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    std::optional<ManagedUiDocumentElement>
    FindManagedUiDocumentElement(const Ref<ScenePresentationRuntime>& presentation, const AssetId document,
                                 const AssetId stableId) noexcept
    {
        try
        {
            const auto element =
                presentation ? presentation->FindUiDocumentElement(EntityId(document), stableId) : std::nullopt;
            return element ? std::optional(Convert(*element)) : std::nullopt;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    std::optional<ManagedUiDocumentElement>
    FindManagedUiDocumentElement(const Ref<ScenePresentationRuntime>& presentation, const AssetId document,
                                 const std::string_view name) noexcept
    {
        try
        {
            const auto element =
                presentation ? presentation->FindUiDocumentElement(EntityId(document), name) : std::nullopt;
            return element ? std::optional(Convert(*element)) : std::nullopt;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    bool ManagedUiDocumentElementAlive(const Ref<ScenePresentationRuntime>& presentation, const AssetId document,
                                       const std::uint64_t documentGeneration, const std::uint64_t element) noexcept
    {
        return presentation && presentation->UiDocumentElementAlive(EntityId(document), documentGeneration, element);
    }

    std::optional<std::string> ReadManagedUiDocumentElementText(const Ref<ScenePresentationRuntime>& presentation,
                                                                const AssetId document,
                                                                const std::uint64_t documentGeneration,
                                                                const std::uint64_t element) noexcept
    {
        return presentation ? presentation->ReadUiDocumentElementText(EntityId(document), documentGeneration, element)
                            : std::nullopt;
    }

    bool SetManagedUiDocumentElementText(const Ref<ScenePresentationRuntime>& presentation, const AssetId document,
                                         const std::uint64_t documentGeneration, const std::uint64_t element,
                                         const std::string_view text) noexcept
    {
        return presentation &&
               presentation->SetUiDocumentElementText(EntityId(document), documentGeneration, element, text);
    }

    std::optional<float> ReadManagedUiDocumentElementValue(const Ref<ScenePresentationRuntime>& presentation,
                                                           const AssetId document,
                                                           const std::uint64_t documentGeneration,
                                                           const std::uint64_t element) noexcept
    {
        return presentation ? presentation->ReadUiDocumentElementValue(EntityId(document), documentGeneration, element)
                            : std::nullopt;
    }

    bool SetManagedUiDocumentElementValue(const Ref<ScenePresentationRuntime>& presentation, const AssetId document,
                                          const std::uint64_t documentGeneration, const std::uint64_t element,
                                          const float value) noexcept
    {
        return presentation &&
               presentation->SetUiDocumentElementValue(EntityId(document), documentGeneration, element, value);
    }

    std::optional<bool> ReadManagedUiDocumentElementFlag(const Ref<ScenePresentationRuntime>& presentation,
                                                         const AssetId document, const std::uint64_t documentGeneration,
                                                         const std::uint64_t element,
                                                         const ManagedUiDocumentFlag property) noexcept
    {
        return presentation ? presentation->ReadUiDocumentElementFlag(EntityId(document), documentGeneration, element,
                                                                      Convert(property))
                            : std::nullopt;
    }

    bool SetManagedUiDocumentElementFlag(const Ref<ScenePresentationRuntime>& presentation, const AssetId document,
                                         const std::uint64_t documentGeneration, const std::uint64_t element,
                                         const ManagedUiDocumentFlag property, const bool value) noexcept
    {
        return presentation && presentation->SetUiDocumentElementFlag(EntityId(document), documentGeneration, element,
                                                                      Convert(property), value);
    }

    bool ConsumeManagedUiDocumentElementEvent(const Ref<ScenePresentationRuntime>& presentation, const AssetId document,
                                              const std::uint64_t documentGeneration, const std::uint64_t element,
                                              const RuntimeUiEventType type) noexcept
    {
        return presentation &&
               presentation->ConsumeUiDocumentElementEvent(EntityId(document), documentGeneration, element, type);
    }

    bool FocusManagedUiDocumentElement(const Ref<ScenePresentationRuntime>& presentation, const AssetId document,
                                       const std::uint64_t documentGeneration, const std::uint64_t element) noexcept
    {
        return presentation && presentation->FocusUiDocumentElement(EntityId(document), documentGeneration, element);
    }
} // namespace Keire::Detail
