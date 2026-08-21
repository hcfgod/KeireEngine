#pragma once

#include "Keire/Api.h"
#include "Keire/Assets/Asset.h"
#include "Keire/Assets/AssetMetadata.h"
#include "Keire/Event.h"

#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Keire
{
    class JobSystem;

    enum class AssetMode : std::uint8_t
    {
        Disabled,
        Development,
        Cooked
    };

    struct AssetDecoderRegistration
    {
        AssetTypeId Type;
        Ref<Asset> Fallback;
        std::function<Ref<Asset>(std::span<const std::byte>)> Decode;
        // Optional owner-thread transaction used when a loaded asset can preserve object identity across reloads.
        // The callback must leave current unchanged on failure and return the object that should remain published.
        std::function<Ref<Asset>(Ref<Asset> current, Ref<Asset> replacement)> ApplyReload;
    };

    struct AssetMountSpecification
    {
        std::filesystem::path CatalogPath;
        int Priority = 0;
        bool AllowOverrides = false;
    };

    struct AssetSystemSpecification
    {
        AssetMode Mode = AssetMode::Disabled;
        std::filesystem::path DevelopmentCatalog = "Library/AssetCache/Runtime/catalog.json";
        std::vector<AssetMountSpecification> Mounts;
        std::vector<AssetDecoderRegistration> Decoders;
        std::size_t WorkerCount = 0;
        std::size_t QueueCapacity = 4096;
        std::size_t ResidentCacheBudgetBytes = std::size_t{512} * 1024U * 1024U;
        std::size_t MaximumAssetBytes = std::size_t{1024} * 1024U * 1024U;
        std::size_t MaximumStreamReadBytes = std::size_t{16} * 1024U * 1024U;
    };

    struct AssetSystemStatistics
    {
        std::size_t KnownAssets = 0;
        std::size_t QueuedAssets = 0;
        std::size_t LoadingAssets = 0;
        std::size_t ResidentBytes = 0;
        std::size_t QueueHighWaterMark = 0;
        std::uint64_t CompletedLoads = 0;
        std::uint64_t FailedLoads = 0;
        std::uint64_t Reloads = 0;
        std::uint64_t Evictions = 0;
    };

    struct AssetLoadedEvent
    {
        AssetId Id;
        AssetTypeId Type;
        std::uint64_t Revision = 0;
        bool Reload = false;
    };

    struct AssetLoadFailedEvent
    {
        AssetId Id;
        AssetTypeId Type;
        AssetDiagnostic Diagnostic;
        bool Reload = false;
    };

    enum class AssetStreamState : std::uint8_t
    {
        Queued,
        Reading,
        Succeeded,
        Failed,
        Cancelled
    };

    enum class AssetStreamSegmentKind : std::uint8_t
    {
        Data,
        Metadata,
        TextureMip,
        MeshLod,
        AudioPage,
        AnimationWindow
    };

    struct AssetStreamSegment
    {
        AssetStreamSegmentKind Kind = AssetStreamSegmentKind::Data;
        std::uint32_t Segment = 0;
        std::uint64_t Offset = 0;
        std::size_t Bytes = 0;
        float WindowStartSeconds = 0.0F;
        float WindowEndSeconds = 0.0F;
    };

    struct AssetStreamLayout
    {
        std::uint32_t Version = 0;
        bool MonolithicCompatibility = true;
        std::vector<AssetStreamSegment> Segments;
    };

    class KEIRE_API AssetStreamOperation final : public RefCounted
    {
      public:
        AssetStreamOperation();
        ~AssetStreamOperation() override;

        [[nodiscard]] AssetStreamState State() const noexcept;
        [[nodiscard]] bool Wait(std::chrono::milliseconds timeout) const;
        [[nodiscard]] std::vector<std::byte> Result() const;
        [[nodiscard]] AssetDiagnostic Diagnostic() const;
        void Cancel() noexcept;

      private:
        friend class AssetSystem;
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };

    class KEIRE_API AssetSystem final : public RefCounted
    {
      public:
        explicit AssetSystem(AssetSystemSpecification specification, Ref<EventBus> events = {},
                             Ref<JobSystem> jobs = {});
        ~AssetSystem() override;

        AssetSystem(const AssetSystem&) = delete;
        AssetSystem& operator=(const AssetSystem&) = delete;

        template <typename T>
            requires std::derived_from<T, Asset>
        [[nodiscard]] AssetHandle<T> Load(const AssetId id, const AssetPriority priority = AssetPriority::Normal)
        {
            return AssetHandle<T>(LoadErased(id, T::StaticType(), priority));
        }

        [[nodiscard]] AssetHandle<Asset> Load(const AssetId id, const AssetTypeId type,
                                              const AssetPriority priority = AssetPriority::Normal)
        {
            return AssetHandle<Asset>(LoadErased(id, type, priority));
        }

        void Mount(const AssetMountSpecification& specification);
        [[nodiscard]] bool Unmount(const std::filesystem::path& catalogPath);
        [[nodiscard]] bool PublishDevelopmentAsset(AssetId id, Ref<Asset> asset);
        [[nodiscard]] bool Reload(AssetId id, AssetPriority priority = AssetPriority::High);
        [[nodiscard]] Ref<AssetStreamOperation> ReadRangeAsync(AssetId id, std::uint64_t offset, std::size_t bytes,
                                                               AssetPriority priority = AssetPriority::Normal);
        [[nodiscard]] std::size_t PumpCompletions();
        [[nodiscard]] std::size_t EvictUnused();
        [[nodiscard]] AssetSystemStatistics Statistics() const;
        [[nodiscard]] std::optional<AssetTypeId> TryGetType(AssetId id) const;
        [[nodiscard]] std::optional<AssetDerivedMetadata> TryGetMetadata(AssetId id) const;
        [[nodiscard]] std::optional<AssetStreamLayout> TryGetStreamLayout(AssetId id) const;
        [[nodiscard]] bool IsOpen() const noexcept;
        void Close() noexcept;

      private:
        class Impl;

        [[nodiscard]] Ref<Detail::AssetHandleState> LoadErased(AssetId id, AssetTypeId type, AssetPriority priority);

        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Keire
