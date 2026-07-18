#pragma once

#include "Keire/Api.h"
#include "Keire/Ref.h"
#include "Keire/Scenes/SceneAsset.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Keire
{
    class Scene;

    namespace Detail
    {
        class SceneState;
    }

    class KEIRE_API SceneObjectHandle final
    {
      public:
        SceneObjectHandle() noexcept = default;
        [[nodiscard]] explicit operator bool() const noexcept;
        [[nodiscard]] AssetId Id() const noexcept { return m_Id; }
        [[nodiscard]] std::optional<SceneObjectDefinition> Snapshot() const;

      private:
        friend class Scene;
        SceneObjectHandle(WeakRef<Detail::SceneState> state, AssetId id) noexcept;
        WeakRef<Detail::SceneState> m_State;
        AssetId m_Id;
    };

    class KEIRE_API Scene final : public RefCounted
    {
      public:
        explicit Scene(AssetId asset, SceneDefinition definition);
        ~Scene() override;

        Scene(const Scene&) = delete;
        Scene& operator=(const Scene&) = delete;

        [[nodiscard]] AssetId Asset() const noexcept;
        [[nodiscard]] std::string Name() const;
        void SetName(std::string name);
        [[nodiscard]] bool IsOpen() const noexcept;
        [[nodiscard]] bool Dirty() const noexcept;
        void MarkDirty() noexcept;
        void MarkSaved() noexcept;
        [[nodiscard]] std::size_t ObjectCount() const noexcept;
        [[nodiscard]] std::vector<SceneObjectDefinition> Objects() const;
        [[nodiscard]] SceneDefinition Snapshot() const;
        [[nodiscard]] SceneObjectHandle Find(AssetId id) const noexcept;
        [[nodiscard]] SceneObjectHandle CreateObject(std::string name = "GameObject", AssetId parent = {});
        [[nodiscard]] SceneObjectHandle DuplicateObject(AssetId id);
        [[nodiscard]] bool DestroyObject(AssetId id);
        [[nodiscard]] bool RenameObject(AssetId id, std::string name);
        [[nodiscard]] bool SetObjectActive(AssetId id, bool active);
        [[nodiscard]] bool SetObjectTransform(AssetId id, SceneTransform transform);
        [[nodiscard]] bool ReparentObject(AssetId id, AssetId parent);
        void Close() noexcept;

      private:
        friend class SceneObjectHandle;
        class Impl;
        [[nodiscard]] std::optional<SceneObjectDefinition> SnapshotObject(AssetId id) const;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Keire
