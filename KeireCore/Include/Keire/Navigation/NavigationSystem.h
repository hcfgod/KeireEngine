#pragma once

#include "Keire/Api.h"
#include "Keire/Math/Math.h"
#include "Keire/Ref.h"

#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Keire
{
    class JobSystem;

    enum class NavigationMode : std::uint8_t
    {
        Disabled,
        Enabled
    };

    struct NavigationSystemSpecification
    {
        NavigationMode Mode = NavigationMode::Disabled;
        std::uint32_t MaximumWorlds = 64;
        std::uint32_t MaximumAgentsPerWorld = 1024;
        std::uint32_t MaximumAsyncQueries = 256;
    };

    class KEIRE_API NavigationNodeId final
    {
      public:
        constexpr NavigationNodeId() noexcept = default;
        explicit constexpr NavigationNodeId(const std::uint32_t value) noexcept : m_Value(value) {}
        [[nodiscard]] constexpr explicit operator bool() const noexcept { return m_Value != 0; }
        [[nodiscard]] constexpr std::uint32_t Value() const noexcept { return m_Value; }
        [[nodiscard]] constexpr auto operator<=>(const NavigationNodeId&) const noexcept = default;

      private:
        std::uint32_t m_Value = 0;
    };

    struct NavigationNode
    {
        NavigationNodeId Id;
        Vector3 Position;
        std::uint32_t Area = 1;
        [[nodiscard]] bool operator==(const NavigationNode&) const noexcept = default;
    };

    struct NavigationEdge
    {
        NavigationNodeId First;
        NavigationNodeId Second;
        float Cost = 1.0F;
        bool Bidirectional = true;
        [[nodiscard]] bool operator==(const NavigationEdge&) const noexcept = default;
    };

    struct NavigationTileData
    {
        std::int32_t X = 0;
        std::int32_t Y = 0;
        std::int32_t Layer = 0;
        std::vector<std::byte> Bytes;
        [[nodiscard]] bool operator==(const NavigationTileData&) const noexcept = default;
    };

    struct NavigationMeshSnapshot
    {
        std::uint64_t Revision = 1;
        std::vector<NavigationNode> Nodes;
        std::vector<NavigationEdge> Edges;
        std::vector<NavigationTileData> Tiles;
    };

    struct NavigationBakeSettings
    {
        float CellSize = 0.3F;
        float CellHeight = 0.2F;
        float AgentHeight = 2.0F;
        float AgentRadius = 0.6F;
        float AgentMaximumClimb = 0.9F;
        float AgentMaximumSlopeDegrees = 45.0F;
        float RegionMinimumArea = 8.0F;
        float RegionMergeArea = 20.0F;
        float EdgeMaximumLength = 12.0F;
        float EdgeMaximumError = 1.3F;
        std::uint32_t MaximumVerticesPerPolygon = 6;
    };

    struct NavigationBakeInput
    {
        std::uint64_t Revision = 1;
        std::vector<Vector3> Vertices;
        std::vector<std::uint32_t> Indices;
        NavigationBakeSettings Settings;
    };

    struct NavigationBakeResult
    {
        NavigationMeshSnapshot Mesh;
        std::uint64_t DependencyHash = 0;
    };

    [[nodiscard]] KEIRE_API NavigationBakeResult BakeNavigationMesh(const NavigationBakeInput& input);

    struct NavigationPathQuery
    {
        Vector3 Start;
        Vector3 End;
        std::uint32_t AreaMask = ~0U;
    };

    enum class NavigationPathState : std::uint8_t
    {
        Pending,
        Succeeded,
        Unreachable,
        Cancelled,
        Stale,
        Failed
    };

    struct NavigationPathResult
    {
        NavigationPathState State = NavigationPathState::Pending;
        std::uint64_t MeshRevision = 0;
        std::vector<Vector3> Points;
        std::string Diagnostic;
    };

    class KEIRE_API NavigationAgentId final
    {
      public:
        constexpr NavigationAgentId() noexcept = default;
        [[nodiscard]] constexpr explicit operator bool() const noexcept { return m_Value != 0; }
        [[nodiscard]] constexpr auto operator<=>(const NavigationAgentId&) const noexcept = default;

      private:
        friend class NavigationWorld;
        explicit constexpr NavigationAgentId(const std::uint64_t value) noexcept : m_Value(value) {}
        std::uint64_t m_Value = 0;
    };

    class KEIRE_API NavigationObstacleId final
    {
      public:
        constexpr NavigationObstacleId() noexcept = default;
        [[nodiscard]] constexpr explicit operator bool() const noexcept { return m_Value != 0; }
        [[nodiscard]] constexpr auto operator<=>(const NavigationObstacleId&) const noexcept = default;

      private:
        friend class NavigationWorld;
        explicit constexpr NavigationObstacleId(const std::uint64_t value) noexcept : m_Value(value) {}
        std::uint64_t m_Value = 0;
    };

    struct NavigationAgentSpecification
    {
        Vector3 Position;
        float Radius = 0.5F;
        float Height = 2.0F;
        float MaximumSpeed = 3.5F;
        float MaximumAcceleration = 8.0F;
        std::uint32_t AreaMask = ~0U;
    };

    enum class NavigationAgentStatus : std::uint8_t
    {
        Idle,
        Seeking,
        Arrived,
        Unreachable
    };

    struct NavigationAgentState
    {
        NavigationAgentId Id;
        NavigationAgentStatus Status = NavigationAgentStatus::Idle;
        Vector3 Position;
        Vector3 Velocity;
        Vector3 Target;
        bool HasTarget = false;
    };

    struct NavigationObstacle
    {
        Vector3 Position;
        float Radius = 0.5F;
        bool Enabled = true;
    };

    struct NavigationRaycastResult
    {
        bool ReachedEnd = false;
        Vector3 Position;
        std::uint64_t MeshRevision = 0;
    };

    KEIRE_API void ValidateNavigationMesh(const NavigationMeshSnapshot& mesh);

    class KEIRE_API NavigationPathOperation final : public RefCounted
    {
      public:
        class Impl;
        ~NavigationPathOperation() override;
        [[nodiscard]] NavigationPathResult Result() const;
        [[nodiscard]] bool Complete() const noexcept;
        [[nodiscard]] bool WaitFor(std::chrono::milliseconds timeout) const;
        void Cancel() noexcept;

      private:
        friend class NavigationWorld;
        template <typename T, typename... Args> friend Ref<T> CreateRef(Args&&... args);
        explicit NavigationPathOperation(std::unique_ptr<Impl> implementation);
        std::unique_ptr<Impl> m_Impl;
    };

    class KEIRE_API NavigationWorld final : public RefCounted
    {
      public:
        class Impl;
        ~NavigationWorld() override;
        [[nodiscard]] bool IsOpen() const noexcept;
        void PublishMesh(std::shared_ptr<const NavigationMeshSnapshot> mesh);
        [[nodiscard]] NavigationPathResult FindPath(const NavigationPathQuery& query) const;
        [[nodiscard]] Ref<NavigationPathOperation> FindPathAsync(NavigationPathQuery query) const;
        [[nodiscard]] Vector3 NearestPoint(Vector3 point, std::uint32_t areaMask = ~0U) const;
        [[nodiscard]] NavigationRaycastResult Raycast(Vector3 start, Vector3 end, std::uint32_t areaMask = ~0U) const;
        [[nodiscard]] NavigationAgentId CreateAgent(const NavigationAgentSpecification& specification);
        [[nodiscard]] bool DestroyAgent(NavigationAgentId agent);
        [[nodiscard]] bool SetAgentTarget(NavigationAgentId agent, Vector3 target);
        [[nodiscard]] std::vector<NavigationAgentState> StepCrowd(float deltaSeconds);
        [[nodiscard]] std::vector<NavigationAgentState> Agents() const;
        [[nodiscard]] NavigationObstacleId AddObstacle(const NavigationObstacle& obstacle);
        [[nodiscard]] bool UpdateObstacle(NavigationObstacleId obstacle, const NavigationObstacle& value);
        [[nodiscard]] bool RemoveObstacle(NavigationObstacleId obstacle);
        [[nodiscard]] std::uint64_t MeshRevision() const noexcept;
        void Close();

      private:
        friend class NavigationSystem;
        template <typename T, typename... Args> friend Ref<T> CreateRef(Args&&... args);
        explicit NavigationWorld(std::unique_ptr<Impl> implementation);
        std::unique_ptr<Impl> m_Impl;
    };

    class KEIRE_API NavigationSystem final : public RefCounted
    {
      public:
        explicit NavigationSystem(NavigationSystemSpecification specification = {}, Ref<JobSystem> jobs = {});
        ~NavigationSystem() override;
        [[nodiscard]] bool IsOpen() const noexcept;
        [[nodiscard]] Ref<NavigationWorld> CreateWorld();
        void Close();

      private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Keire
