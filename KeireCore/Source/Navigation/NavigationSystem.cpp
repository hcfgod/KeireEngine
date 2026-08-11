#include "Keire/Navigation/NavigationSystem.h"

#include "Keire/Jobs/JobSystem.h"

#include <DetourAlloc.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <Recast.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>
#include <queue>
#include <ranges>
#include <set>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <tuple>
#include <utility>

namespace Keire
{
    namespace
    {
        struct NavigationServiceState final
        {
            std::atomic<bool> Open{true};
            std::atomic<std::uint32_t> Worlds{0};
            std::atomic<std::uint32_t> AsyncQueries{0};
            std::uint32_t MaximumWorlds = 0;
            std::uint32_t MaximumAgentsPerWorld = 0;
            std::uint32_t MaximumAsyncQueries = 0;
            Ref<JobSystem> Scheduler;
            Ref<JobScope> Scope;
            bool OwnScheduler = false;
        };

        struct NavigationWorldState final
        {
            struct Agent final
            {
                NavigationAgentState State;
                NavigationAgentSpecification Specification;
                std::vector<Vector3> Path;
                std::size_t NextPoint = 0;
            };
            std::atomic<bool> Open{true};
            std::atomic<std::uint64_t> Revision{0};
            std::atomic<std::uint64_t> Generation{0};
            mutable std::mutex Mutex;
            std::shared_ptr<const NavigationMeshSnapshot> Mesh;
            std::map<NavigationAgentId, Agent> Agents;
            std::map<NavigationObstacleId, NavigationObstacle> Obstacles;
            std::uint64_t NextAgent = 1;
            std::uint64_t NextObstacle = 1;
        };

        [[nodiscard]] float DistanceSquared(const Vector3 first, const Vector3 second) noexcept
        {
            const auto x = first.X - second.X;
            const auto y = first.Y - second.Y;
            const auto z = first.Z - second.Z;
            return x * x + y * y + z * z;
        }

        [[nodiscard]] float VectorLength(const Vector3 value) noexcept
        {
            return std::sqrt(value.X * value.X + value.Y * value.Y + value.Z * value.Z);
        }

        [[nodiscard]] bool ValidBakeSettings(const NavigationBakeSettings& settings) noexcept
        {
            return std::isfinite(settings.CellSize) && settings.CellSize > 0.0F && std::isfinite(settings.CellHeight) &&
                   settings.CellHeight > 0.0F && std::isfinite(settings.AgentHeight) && settings.AgentHeight > 0.0F &&
                   std::isfinite(settings.AgentRadius) && settings.AgentRadius >= 0.0F &&
                   std::isfinite(settings.AgentMaximumClimb) && settings.AgentMaximumClimb >= 0.0F &&
                   std::isfinite(settings.AgentMaximumSlopeDegrees) && settings.AgentMaximumSlopeDegrees >= 0.0F &&
                   settings.AgentMaximumSlopeDegrees < 90.0F && std::isfinite(settings.RegionMinimumArea) &&
                   settings.RegionMinimumArea >= 0.0F && std::isfinite(settings.RegionMergeArea) &&
                   settings.RegionMergeArea >= 0.0F && std::isfinite(settings.EdgeMaximumLength) &&
                   settings.EdgeMaximumLength >= 0.0F && std::isfinite(settings.EdgeMaximumError) &&
                   settings.EdgeMaximumError >= 0.0F && settings.MaximumVerticesPerPolygon >= 3 &&
                   settings.MaximumVerticesPerPolygon <= 12;
        }

        [[nodiscard]] bool Blocked(const Vector3 point, const std::span<const NavigationObstacle> obstacles) noexcept
        {
            return std::ranges::any_of(obstacles,
                                       [point](const NavigationObstacle& obstacle)
                                       {
                                           return obstacle.Enabled && DistanceSquared(point, obstacle.Position) <=
                                                                          obstacle.Radius * obstacle.Radius;
                                       });
        }

        template <typename Value> void HashValue(std::uint64_t& hash, const Value value) noexcept
        {
            const auto bytes = std::bit_cast<std::array<std::byte, sizeof(Value)>>(value);
            for (const auto byte : bytes)
            {
                hash ^= std::to_integer<std::uint8_t>(byte);
                hash *= 1099511628211ULL;
            }
        }

        [[nodiscard]] NavigationPathResult SolveDetourPath(const NavigationMeshSnapshot& mesh,
                                                           const NavigationPathQuery& query,
                                                           std::stop_token cancellation);

        [[nodiscard]] NavigationPathResult SolvePath(const NavigationMeshSnapshot& mesh,
                                                     const NavigationPathQuery& query,
                                                     const std::stop_token cancellation = {},
                                                     const std::span<const NavigationObstacle> obstacles = {})
        {
            NavigationPathResult result{.State = NavigationPathState::Unreachable, .MeshRevision = mesh.Revision};
            if (!Math::IsFinite(query.Start) || !Math::IsFinite(query.End) || query.AreaMask == 0)
                throw std::invalid_argument("Navigation path query is invalid.");
            if (!mesh.Tiles.empty() && obstacles.empty())
                return SolveDetourPath(mesh, query, cancellation);

            const NavigationNode* start = nullptr;
            const NavigationNode* end = nullptr;
            for (const auto& node : mesh.Nodes)
            {
                if ((node.Area & query.AreaMask) == 0)
                    continue;
                if (Blocked(node.Position, obstacles))
                    continue;
                if (!start ||
                    DistanceSquared(node.Position, query.Start) < DistanceSquared(start->Position, query.Start) ||
                    (DistanceSquared(node.Position, query.Start) == DistanceSquared(start->Position, query.Start) &&
                     node.Id < start->Id))
                    start = &node;
                if (!end || DistanceSquared(node.Position, query.End) < DistanceSquared(end->Position, query.End) ||
                    (DistanceSquared(node.Position, query.End) == DistanceSquared(end->Position, query.End) &&
                     node.Id < end->Id))
                    end = &node;
            }
            if (!start || !end)
                return result;

            using QueueEntry = std::pair<float, NavigationNodeId>;
            std::priority_queue<QueueEntry, std::vector<QueueEntry>, std::greater<>> queue;
            std::map<NavigationNodeId, float> distances;
            std::map<NavigationNodeId, NavigationNodeId> previous;
            std::map<NavigationNodeId, const NavigationNode*> nodes;
            for (const auto& node : mesh.Nodes)
            {
                nodes.emplace(node.Id, &node);
                distances.emplace(node.Id, std::numeric_limits<float>::infinity());
            }
            distances[start->Id] = 0.0F;
            queue.emplace(0.0F, start->Id);
            while (!queue.empty())
            {
                if (cancellation.stop_requested())
                    return {.State = NavigationPathState::Cancelled, .MeshRevision = mesh.Revision};
                const auto [distance, node] = queue.top();
                queue.pop();
                if (distance != distances[node])
                    continue;
                if (node == end->Id)
                    break;
                for (const auto& edge : mesh.Edges)
                {
                    NavigationNodeId neighbor;
                    if (edge.First == node)
                        neighbor = edge.Second;
                    else if (edge.Bidirectional && edge.Second == node)
                        neighbor = edge.First;
                    else
                        continue;
                    if ((nodes.at(neighbor)->Area & query.AreaMask) == 0)
                        continue;
                    if (Blocked(nodes.at(neighbor)->Position, obstacles))
                        continue;
                    const auto next = distance + edge.Cost;
                    if (next < distances[neighbor] ||
                        (next == distances[neighbor] && (!previous.contains(neighbor) || node < previous[neighbor])))
                    {
                        distances[neighbor] = next;
                        previous[neighbor] = node;
                        queue.emplace(next, neighbor);
                    }
                }
            }
            if (!std::isfinite(distances[end->Id]))
                return result;

            std::vector<Vector3> reversed{query.End};
            auto node = end->Id;
            while (node != start->Id)
            {
                reversed.push_back(nodes.at(node)->Position);
                node = previous.at(node);
            }
            reversed.push_back(nodes.at(start->Id)->Position);
            reversed.push_back(query.Start);
            result.Points.assign(reversed.rbegin(), reversed.rend());
            result.State = NavigationPathState::Succeeded;
            return result;
        }

        [[nodiscard]] NavigationPathResult SolveDetourPath(const NavigationMeshSnapshot& mesh,
                                                           const NavigationPathQuery& query,
                                                           const std::stop_token cancellation)
        {
            NavigationPathResult result{.State = NavigationPathState::Unreachable, .MeshRevision = mesh.Revision};
            if (!Math::IsFinite(query.Start) || !Math::IsFinite(query.End) || query.AreaMask == 0)
                throw std::invalid_argument("Navigation path query is invalid.");
            if (cancellation.stop_requested())
                return {.State = NavigationPathState::Cancelled, .MeshRevision = mesh.Revision};
            if (mesh.Tiles.size() != 1)
                return {.State = NavigationPathState::Failed,
                        .MeshRevision = mesh.Revision,
                        .Diagnostic = "This runtime supports one resident Detour tile per snapshot."};

            auto* data = static_cast<unsigned char*>(dtAlloc(mesh.Tiles.front().Bytes.size(), DT_ALLOC_PERM));
            if (!data)
                throw std::bad_alloc();
            std::memcpy(data, mesh.Tiles.front().Bytes.data(), mesh.Tiles.front().Bytes.size());
            const auto navigation =
                std::unique_ptr<dtNavMesh, decltype(&dtFreeNavMesh)>(dtAllocNavMesh(), &dtFreeNavMesh);
            if (!navigation)
            {
                dtFree(data);
                throw std::bad_alloc();
            }
            if (dtStatusFailed(
                    navigation->init(data, static_cast<int>(mesh.Tiles.front().Bytes.size()), DT_TILE_FREE_DATA)))
            {
                dtFree(data);
                return {.State = NavigationPathState::Failed,
                        .MeshRevision = mesh.Revision,
                        .Diagnostic = "Detour rejected cooked navigation tile data."};
            }
            const auto navigationQuery = std::unique_ptr<dtNavMeshQuery, decltype(&dtFreeNavMeshQuery)>(
                dtAllocNavMeshQuery(), &dtFreeNavMeshQuery);
            if (!navigationQuery || dtStatusFailed(navigationQuery->init(navigation.get(), 4096)))
                throw std::runtime_error("Detour query allocation failed.");

            dtQueryFilter filter;
            filter.setIncludeFlags(static_cast<unsigned short>(query.AreaMask & 0xffffU));
            const float extent[]{2.0F, 4.0F, 2.0F};
            const float start[]{query.Start.X, query.Start.Y, query.Start.Z};
            const float end[]{query.End.X, query.End.Y, query.End.Z};
            float nearestStart[3]{};
            float nearestEnd[3]{};
            dtPolyRef startReference = 0;
            dtPolyRef endReference = 0;
            if (dtStatusFailed(
                    navigationQuery->findNearestPoly(start, extent, &filter, &startReference, nearestStart)) ||
                dtStatusFailed(navigationQuery->findNearestPoly(end, extent, &filter, &endReference, nearestEnd)) ||
                startReference == 0 || endReference == 0)
                return result;

            std::array<dtPolyRef, 256> polygonPath{};
            int polygonCount = 0;
            if (dtStatusFailed(navigationQuery->findPath(startReference, endReference, nearestStart, nearestEnd,
                                                         &filter, polygonPath.data(), &polygonCount,
                                                         static_cast<int>(polygonPath.size()))) ||
                polygonCount == 0)
                return result;
            if (cancellation.stop_requested())
                return {.State = NavigationPathState::Cancelled, .MeshRevision = mesh.Revision};

            std::array<float, std::size_t{256} * 3U> straightPath{};
            int straightCount = 0;
            if (dtStatusFailed(navigationQuery->findStraightPath(nearestStart, nearestEnd, polygonPath.data(),
                                                                 polygonCount, straightPath.data(), nullptr, nullptr,
                                                                 &straightCount, 256)) ||
                straightCount == 0)
                return result;
            result.Points.reserve(static_cast<std::size_t>(straightCount) + 2U);
            result.Points.push_back(query.Start);
            for (int point = 1; point + 1 < straightCount; ++point)
            {
                const auto pointOffset = static_cast<std::size_t>(point) * 3U;
                result.Points.push_back(
                    {straightPath[pointOffset], straightPath[pointOffset + 1U], straightPath[pointOffset + 2U]});
            }
            result.Points.push_back(query.End);
            result.State = NavigationPathState::Succeeded;
            return result;
        }
    } // namespace

    NavigationBakeResult BakeNavigationMesh(const NavigationBakeInput& input)
    {
        if (input.Revision == 0 || input.Vertices.size() < 3 ||
            input.Vertices.size() > std::size_t{16} * 1024U * 1024U || input.Indices.empty() ||
            input.Indices.size() > std::size_t{48} * 1024U * 1024U || input.Indices.size() % 3U != 0 ||
            !ValidBakeSettings(input.Settings))
            throw std::invalid_argument("Navigation bake input header or settings are invalid.");

        std::vector<float> vertices;
        vertices.reserve(input.Vertices.size() * 3U);
        for (const auto vertex : input.Vertices)
        {
            if (!Math::IsFinite(vertex))
                throw std::invalid_argument("Navigation bake input contains a non-finite vertex.");
            vertices.insert(vertices.end(), {vertex.X, vertex.Y, vertex.Z});
        }
        std::vector<int> indices;
        indices.reserve(input.Indices.size());
        for (const auto index : input.Indices)
        {
            if (index >= input.Vertices.size() || index > static_cast<std::uint32_t>(std::numeric_limits<int>::max()))
                throw std::invalid_argument("Navigation bake input contains an out-of-range index.");
            indices.push_back(static_cast<int>(index));
        }

        std::uint64_t dependencyHash = 14695981039346656037ULL;
        HashValue(dependencyHash, input.Revision);
        for (const auto value : vertices)
            HashValue(dependencyHash, std::bit_cast<std::uint32_t>(value));
        for (const auto value : input.Indices)
            HashValue(dependencyHash, value);
        const auto& settings = input.Settings;
        for (const auto value :
             {settings.CellSize, settings.CellHeight, settings.AgentHeight, settings.AgentRadius,
              settings.AgentMaximumClimb, settings.AgentMaximumSlopeDegrees, settings.RegionMinimumArea,
              settings.RegionMergeArea, settings.EdgeMaximumLength, settings.EdgeMaximumError})
            HashValue(dependencyHash, std::bit_cast<std::uint32_t>(value));
        HashValue(dependencyHash, settings.MaximumVerticesPerPolygon);

        rcConfig config{};
        config.cs = settings.CellSize;
        config.ch = settings.CellHeight;
        config.walkableSlopeAngle = settings.AgentMaximumSlopeDegrees;
        config.walkableHeight = static_cast<int>(std::ceil(settings.AgentHeight / config.ch));
        config.walkableClimb = static_cast<int>(std::floor(settings.AgentMaximumClimb / config.ch));
        config.walkableRadius = static_cast<int>(std::ceil(settings.AgentRadius / config.cs));
        config.maxEdgeLen = static_cast<int>(settings.EdgeMaximumLength / config.cs);
        config.maxSimplificationError = settings.EdgeMaximumError;
        const auto minimumRegionCells = static_cast<int>(settings.RegionMinimumArea / config.cs);
        const auto mergeRegionCells = static_cast<int>(settings.RegionMergeArea / config.cs);
        config.minRegionArea = minimumRegionCells * minimumRegionCells;
        config.mergeRegionArea = mergeRegionCells * mergeRegionCells;
        config.maxVertsPerPoly = static_cast<int>(settings.MaximumVerticesPerPolygon);
        config.detailSampleDist = 0.0F;
        config.detailSampleMaxError = 0.0F;
        rcCalcBounds(vertices.data(), static_cast<int>(input.Vertices.size()), config.bmin, config.bmax);
        if (config.bmax[1] - config.bmin[1] < config.ch)
        {
            config.bmin[1] -= config.ch;
            config.bmax[1] += settings.AgentHeight + config.ch;
        }
        rcCalcGridSize(config.bmin, config.bmax, config.cs, &config.width, &config.height);
        if (config.width <= 0 || config.height <= 0 || config.width > 65'536 || config.height > 65'536)
            throw std::invalid_argument("Navigation bake grid dimensions are invalid or excessive.");

        rcContext context;
        const auto heightfield =
            std::unique_ptr<rcHeightfield, decltype(&rcFreeHeightField)>(rcAllocHeightfield(), &rcFreeHeightField);
        if (!heightfield || !rcCreateHeightfield(&context, *heightfield, config.width, config.height, config.bmin,
                                                 config.bmax, config.cs, config.ch))
            throw std::runtime_error("Recast heightfield allocation failed.");
        const auto triangleCount = static_cast<int>(indices.size() / 3U);
        std::vector<unsigned char> areas(static_cast<std::size_t>(triangleCount), RC_NULL_AREA);
        rcMarkWalkableTriangles(&context, config.walkableSlopeAngle, vertices.data(),
                                static_cast<int>(input.Vertices.size()), indices.data(), triangleCount, areas.data());
        if (!rcRasterizeTriangles(&context, vertices.data(), static_cast<int>(input.Vertices.size()), indices.data(),
                                  areas.data(), triangleCount, *heightfield, config.walkableClimb))
            throw std::runtime_error("Recast triangle rasterization failed.");
        rcFilterLowHangingWalkableObstacles(&context, config.walkableClimb, *heightfield);
        rcFilterLedgeSpans(&context, config.walkableHeight, config.walkableClimb, *heightfield);
        rcFilterWalkableLowHeightSpans(&context, config.walkableHeight, *heightfield);

        const auto compact = std::unique_ptr<rcCompactHeightfield, decltype(&rcFreeCompactHeightfield)>(
            rcAllocCompactHeightfield(), &rcFreeCompactHeightfield);
        if (!compact ||
            !rcBuildCompactHeightfield(&context, config.walkableHeight, config.walkableClimb, *heightfield, *compact) ||
            !rcErodeWalkableArea(&context, config.walkableRadius, *compact) ||
            !rcBuildDistanceField(&context, *compact) ||
            !rcBuildRegions(&context, *compact, config.borderSize, config.minRegionArea, config.mergeRegionArea))
            throw std::runtime_error("Recast compact heightfield build failed.");
        const auto contours =
            std::unique_ptr<rcContourSet, decltype(&rcFreeContourSet)>(rcAllocContourSet(), &rcFreeContourSet);
        if (!contours || !rcBuildContours(&context, *compact, config.maxSimplificationError, config.maxEdgeLen,
                                          *contours, RC_CONTOUR_TESS_WALL_EDGES))
            throw std::runtime_error("Recast contour build failed.");
        const auto polyMesh =
            std::unique_ptr<rcPolyMesh, decltype(&rcFreePolyMesh)>(rcAllocPolyMesh(), &rcFreePolyMesh);
        if (!polyMesh || !rcBuildPolyMesh(&context, *contours, config.maxVertsPerPoly, *polyMesh) ||
            polyMesh->npolys <= 0)
            throw std::runtime_error("Recast produced no walkable navigation polygons.");
        for (int polygon = 0; polygon < polyMesh->npolys; ++polygon)
        {
            if (polyMesh->areas[polygon] == RC_WALKABLE_AREA)
                polyMesh->areas[polygon] = 1;
            polyMesh->flags[polygon] = 1;
        }

        NavigationBakeResult result;
        result.DependencyHash = dependencyHash;
        result.Mesh.Revision = input.Revision;
        result.Mesh.Nodes.reserve(static_cast<std::size_t>(polyMesh->npolys));
        for (int polygon = 0; polygon < polyMesh->npolys; ++polygon)
        {
            const auto polygonOffset = static_cast<std::size_t>(polygon) * static_cast<std::size_t>(polyMesh->nvp) * 2U;
            const auto* polygonData = &polyMesh->polys[polygonOffset];
            Vector3 center{};
            std::uint32_t count = 0;
            for (int corner = 0; corner < polyMesh->nvp && polygonData[corner] != RC_MESH_NULL_IDX; ++corner)
            {
                const auto vertex = polygonData[corner];
                const auto vertexOffset = static_cast<std::size_t>(vertex) * 3U;
                center.X += polyMesh->bmin[0] + static_cast<float>(polyMesh->verts[vertexOffset]) * polyMesh->cs;
                center.Y += polyMesh->bmin[1] + static_cast<float>(polyMesh->verts[vertexOffset + 1U]) * polyMesh->ch;
                center.Z += polyMesh->bmin[2] + static_cast<float>(polyMesh->verts[vertexOffset + 2U]) * polyMesh->cs;
                ++count;
            }
            if (count == 0)
                throw std::runtime_error("Recast produced an empty polygon.");
            center.X /= static_cast<float>(count);
            center.Y /= static_cast<float>(count);
            center.Z /= static_cast<float>(count);
            result.Mesh.Nodes.push_back({NavigationNodeId(static_cast<std::uint32_t>(polygon + 1)), center, 1U});
        }

        std::set<std::pair<std::uint32_t, std::uint32_t>> edges;
        for (int polygon = 0; polygon < polyMesh->npolys; ++polygon)
        {
            const auto* neighbors = &polyMesh->polys[polygon * polyMesh->nvp * 2 + polyMesh->nvp];
            for (int edge = 0; edge < polyMesh->nvp; ++edge)
            {
                const auto neighbor = neighbors[edge];
                if (neighbor == RC_MESH_NULL_IDX || (neighbor & RC_BORDER_REG) != 0 ||
                    neighbor >= static_cast<unsigned short>(polyMesh->npolys))
                    continue;
                const auto first = static_cast<std::uint32_t>(polygon + 1);
                const auto second = static_cast<std::uint32_t>(neighbor + 1);
                edges.emplace(std::min(first, second), std::max(first, second));
            }
        }
        for (const auto& [first, second] : edges)
        {
            const auto& firstNode = result.Mesh.Nodes[first - 1U];
            const auto& secondNode = result.Mesh.Nodes[second - 1U];
            result.Mesh.Edges.push_back({firstNode.Id, secondNode.Id,
                                         std::sqrt(DistanceSquared(firstNode.Position, secondNode.Position)), true});
        }

        dtNavMeshCreateParams parameters{};
        parameters.verts = polyMesh->verts;
        parameters.vertCount = polyMesh->nverts;
        parameters.polys = polyMesh->polys;
        parameters.polyAreas = polyMesh->areas;
        parameters.polyFlags = polyMesh->flags;
        parameters.polyCount = polyMesh->npolys;
        parameters.nvp = polyMesh->nvp;
        parameters.tileX = 0;
        parameters.tileY = 0;
        parameters.tileLayer = 0;
        std::ranges::copy(polyMesh->bmin, parameters.bmin);
        std::ranges::copy(polyMesh->bmax, parameters.bmax);
        parameters.walkableHeight = settings.AgentHeight;
        parameters.walkableRadius = settings.AgentRadius;
        parameters.walkableClimb = settings.AgentMaximumClimb;
        parameters.cs = polyMesh->cs;
        parameters.ch = polyMesh->ch;
        parameters.buildBvTree = true;
        unsigned char* tileData = nullptr;
        int tileDataSize = 0;
        if (!dtCreateNavMeshData(&parameters, &tileData, &tileDataSize) || !tileData || tileDataSize <= 0)
            throw std::runtime_error("Detour navigation tile creation failed.");
        NavigationTileData tile;
        tile.Bytes.resize(static_cast<std::size_t>(tileDataSize));
        std::memcpy(tile.Bytes.data(), tileData, tile.Bytes.size());
        dtFree(tileData);
        result.Mesh.Tiles.push_back(std::move(tile));
        ValidateNavigationMesh(result.Mesh);
        return result;
    }

    void ValidateNavigationMesh(const NavigationMeshSnapshot& mesh)
    {
        if (mesh.Revision == 0 || mesh.Nodes.empty() || mesh.Nodes.size() > std::size_t{4} * 1024U * 1024U ||
            mesh.Edges.size() > std::size_t{16} * 1024U * 1024U)
            throw std::invalid_argument("Navigation mesh header is invalid.");
        std::set<NavigationNodeId> nodes;
        for (const auto& node : mesh.Nodes)
            if (!node.Id || !Math::IsFinite(node.Position) || node.Area == 0 || !nodes.insert(node.Id).second)
                throw std::invalid_argument("Navigation mesh contains an invalid or duplicate node.");
        std::set<std::pair<NavigationNodeId, NavigationNodeId>> edges;
        for (const auto& edge : mesh.Edges)
        {
            if (!nodes.contains(edge.First) || !nodes.contains(edge.Second) || edge.First == edge.Second ||
                !std::isfinite(edge.Cost) || edge.Cost <= 0.0F)
                throw std::invalid_argument("Navigation mesh contains an invalid edge.");
            const auto key = edge.Bidirectional && edge.Second < edge.First ? std::pair{edge.Second, edge.First}
                                                                            : std::pair{edge.First, edge.Second};
            if (!edges.insert(key).second)
                throw std::invalid_argument("Navigation mesh contains a duplicate edge.");
        }
        if (mesh.Tiles.size() > 4096)
            throw std::invalid_argument("Navigation mesh contains too many streamed tiles.");
        std::set<std::tuple<std::int32_t, std::int32_t, std::int32_t>> tiles;
        for (const auto& tile : mesh.Tiles)
            if (tile.Bytes.empty() || tile.Bytes.size() > std::size_t{64} * 1024U * 1024U ||
                !tiles.emplace(tile.X, tile.Y, tile.Layer).second)
                throw std::invalid_argument("Navigation mesh contains invalid or duplicate tile data.");
    }

    class NavigationPathOperation::Impl final
    {
      public:
        mutable std::mutex Mutex;
        mutable std::condition_variable Completed;
        NavigationPathResult Value;
        JobHandle Worker;
        std::stop_source Cancellation;
    };

    NavigationPathOperation::NavigationPathOperation(std::unique_ptr<Impl> implementation)
        : m_Impl(std::move(implementation))
    {
    }
    NavigationPathOperation::~NavigationPathOperation() = default;
    NavigationPathResult NavigationPathOperation::Result() const
    {
        std::scoped_lock lock(m_Impl->Mutex);
        return m_Impl->Value;
    }
    bool NavigationPathOperation::Complete() const noexcept
    {
        std::scoped_lock lock(m_Impl->Mutex);
        return m_Impl->Value.State != NavigationPathState::Pending;
    }
    bool NavigationPathOperation::WaitFor(const std::chrono::milliseconds timeout) const
    {
        if (timeout.count() < 0)
            throw std::invalid_argument("Navigation path wait timeout cannot be negative.");
        std::unique_lock lock(m_Impl->Mutex);
        return m_Impl->Completed.wait_for(lock, timeout,
                                          [this] { return m_Impl->Value.State != NavigationPathState::Pending; });
    }
    void NavigationPathOperation::Cancel() noexcept { m_Impl->Cancellation.request_stop(); }

    class NavigationWorld::Impl final
    {
      public:
        Impl(std::shared_ptr<NavigationServiceState> service, std::shared_ptr<NavigationWorldState> state)
            : Service(std::move(service)), State(std::move(state)), Owner(std::this_thread::get_id())
        {
        }
        ~Impl() { Service->Worlds.fetch_sub(1, std::memory_order_relaxed); }
        void RequireOwner(const char* operation) const
        {
            if (std::this_thread::get_id() != Owner)
                throw std::logic_error(std::string("NavigationWorld::") + operation + " must run on the owner thread.");
            if (!State->Open.load(std::memory_order_acquire) || !Service->Open.load(std::memory_order_acquire))
                throw std::logic_error("NavigationWorld is closed or its NavigationSystem is unavailable.");
        }
        std::shared_ptr<NavigationServiceState> Service;
        std::shared_ptr<NavigationWorldState> State;
        std::thread::id Owner;
    };

    NavigationWorld::NavigationWorld(std::unique_ptr<Impl> implementation) : m_Impl(std::move(implementation)) {}
    NavigationWorld::~NavigationWorld() = default;
    bool NavigationWorld::IsOpen() const noexcept
    {
        return m_Impl->State->Open.load(std::memory_order_acquire) &&
               m_Impl->Service->Open.load(std::memory_order_acquire);
    }
    void NavigationWorld::PublishMesh(std::shared_ptr<const NavigationMeshSnapshot> mesh)
    {
        m_Impl->RequireOwner("PublishMesh");
        if (!mesh)
            throw std::invalid_argument("Navigation mesh snapshot is empty.");
        ValidateNavigationMesh(*mesh);
        std::scoped_lock lock(m_Impl->State->Mutex);
        if (mesh->Revision <= m_Impl->State->Revision.load(std::memory_order_relaxed))
            throw std::invalid_argument("Navigation mesh revision must increase monotonically.");
        m_Impl->State->Mesh = std::move(mesh);
        m_Impl->State->Revision.store(m_Impl->State->Mesh->Revision, std::memory_order_release);
        m_Impl->State->Generation.fetch_add(1, std::memory_order_release);
    }
    NavigationPathResult NavigationWorld::FindPath(const NavigationPathQuery& query) const
    {
        m_Impl->RequireOwner("FindPath");
        std::shared_ptr<const NavigationMeshSnapshot> mesh;
        std::vector<NavigationObstacle> obstacles;
        {
            std::scoped_lock lock(m_Impl->State->Mutex);
            mesh = m_Impl->State->Mesh;
            for (const auto& [id, obstacle] : m_Impl->State->Obstacles)
            {
                (void)id;
                obstacles.push_back(obstacle);
            }
        }
        if (!mesh)
            return {.State = NavigationPathState::Failed, .Diagnostic = "Navigation world has no published mesh."};
        return SolvePath(*mesh, query, {}, obstacles);
    }
    Ref<NavigationPathOperation> NavigationWorld::FindPathAsync(NavigationPathQuery query) const
    {
        m_Impl->RequireOwner("FindPathAsync");
        std::shared_ptr<const NavigationMeshSnapshot> mesh;
        std::vector<NavigationObstacle> obstacles;
        std::uint64_t generation = 0;
        {
            std::scoped_lock lock(m_Impl->State->Mutex);
            mesh = m_Impl->State->Mesh;
            generation = m_Impl->State->Generation.load(std::memory_order_relaxed);
            for (const auto& [id, obstacle] : m_Impl->State->Obstacles)
            {
                (void)id;
                obstacles.push_back(obstacle);
            }
        }
        auto operation = CreateRef<NavigationPathOperation>(std::make_unique<NavigationPathOperation::Impl>());
        if (!mesh)
        {
            operation->m_Impl->Value = {.State = NavigationPathState::Failed,
                                        .Diagnostic = "Navigation world has no published mesh."};
            return operation;
        }
        const auto queryCount = m_Impl->Service->AsyncQueries.fetch_add(1, std::memory_order_acq_rel) + 1;
        if (queryCount > m_Impl->Service->MaximumAsyncQueries)
        {
            m_Impl->Service->AsyncQueries.fetch_sub(1, std::memory_order_acq_rel);
            throw std::runtime_error("Navigation asynchronous query capacity was exhausted.");
        }
        const auto state = m_Impl->State;
        const auto service = m_Impl->Service;
        try
        {
            JobDescription description;
            description.Name = "Navigation path query";
            description.Domain = JobDomain::Simulation;
            operation->m_Impl->Worker = m_Impl->Service->Scope->Submit(
                std::move(description),
                [operation, state, service, mesh, query, generation,
                 obstacles = std::move(obstacles)](JobContext& context)
                {
                    std::stop_source cancellation;
                    std::stop_callback jobCancellation(context.StopToken(), [&] { cancellation.request_stop(); });
                    std::stop_callback operationCancellation(operation->m_Impl->Cancellation.get_token(),
                                                             [&] { cancellation.request_stop(); });
                    NavigationPathResult result;
                    try
                    {
                        result = SolvePath(*mesh, query, cancellation.get_token(), obstacles);
                    }
                    catch (const std::exception& exception)
                    {
                        result = {.State = NavigationPathState::Failed,
                                  .MeshRevision = mesh->Revision,
                                  .Diagnostic = exception.what()};
                    }
                    catch (...)
                    {
                        result = {.State = NavigationPathState::Failed,
                                  .MeshRevision = mesh->Revision,
                                  .Diagnostic = "Navigation query reported an unknown failure."};
                    }
                    if (result.State != NavigationPathState::Cancelled &&
                        (!state->Open.load(std::memory_order_acquire) ||
                         !service->Open.load(std::memory_order_acquire)))
                        result.State = NavigationPathState::Cancelled;
                    else if (result.State != NavigationPathState::Cancelled &&
                             state->Generation.load(std::memory_order_acquire) != generation)
                        result.State = NavigationPathState::Stale;
                    {
                        std::scoped_lock lock(operation->m_Impl->Mutex);
                        operation->m_Impl->Value = std::move(result);
                    }
                    operation->m_Impl->Completed.notify_all();
                    service->AsyncQueries.fetch_sub(1, std::memory_order_acq_rel);
                });
        }
        catch (...)
        {
            m_Impl->Service->AsyncQueries.fetch_sub(1, std::memory_order_acq_rel);
            throw;
        }
        return operation;
    }

    Vector3 NavigationWorld::NearestPoint(const Vector3 point, const std::uint32_t areaMask) const
    {
        m_Impl->RequireOwner("NearestPoint");
        if (!Math::IsFinite(point) || areaMask == 0)
            throw std::invalid_argument("Navigation nearest-point query is invalid.");
        std::scoped_lock lock(m_Impl->State->Mutex);
        if (!m_Impl->State->Mesh)
            throw std::logic_error("Navigation world has no published mesh.");
        const NavigationNode* nearest = nullptr;
        std::vector<NavigationObstacle> obstacles;
        for (const auto& [id, obstacle] : m_Impl->State->Obstacles)
        {
            (void)id;
            obstacles.push_back(obstacle);
        }
        for (const auto& node : m_Impl->State->Mesh->Nodes)
            if ((node.Area & areaMask) != 0 && !Blocked(node.Position, obstacles) &&
                (!nearest || DistanceSquared(node.Position, point) < DistanceSquared(nearest->Position, point) ||
                 (DistanceSquared(node.Position, point) == DistanceSquared(nearest->Position, point) &&
                  node.Id < nearest->Id)))
                nearest = &node;
        if (!nearest)
            throw std::runtime_error("Navigation nearest-point query found no walkable node.");
        return nearest->Position;
    }

    NavigationRaycastResult NavigationWorld::Raycast(const Vector3 start, const Vector3 end,
                                                     const std::uint32_t areaMask) const
    {
        const auto path = FindPath({start, end, areaMask});
        if (path.State != NavigationPathState::Succeeded)
            return {.ReachedEnd = false, .Position = NearestPoint(start, areaMask), .MeshRevision = path.MeshRevision};
        return {.ReachedEnd = true, .Position = end, .MeshRevision = path.MeshRevision};
    }

    NavigationAgentId NavigationWorld::CreateAgent(const NavigationAgentSpecification& specification)
    {
        m_Impl->RequireOwner("CreateAgent");
        if (!Math::IsFinite(specification.Position) || !std::isfinite(specification.Radius) ||
            !std::isfinite(specification.Height) || !std::isfinite(specification.MaximumSpeed) ||
            !std::isfinite(specification.MaximumAcceleration) || specification.Radius <= 0.0F ||
            specification.Height <= 0.0F || specification.MaximumSpeed <= 0.0F ||
            specification.MaximumAcceleration <= 0.0F || specification.AreaMask == 0)
            throw std::invalid_argument("Navigation agent specification is invalid.");
        std::scoped_lock lock(m_Impl->State->Mutex);
        if (m_Impl->State->Agents.size() >= m_Impl->Service->MaximumAgentsPerWorld)
            throw std::runtime_error("Navigation crowd capacity was exhausted.");
        const NavigationAgentId id(m_Impl->State->NextAgent++);
        NavigationWorldState::Agent agent;
        agent.State.Id = id;
        agent.State.Position = specification.Position;
        agent.Specification = specification;
        m_Impl->State->Agents.emplace(id, std::move(agent));
        return id;
    }

    bool NavigationWorld::DestroyAgent(const NavigationAgentId agent)
    {
        m_Impl->RequireOwner("DestroyAgent");
        std::scoped_lock lock(m_Impl->State->Mutex);
        return m_Impl->State->Agents.erase(agent) != 0;
    }

    bool NavigationWorld::SetAgentTarget(const NavigationAgentId agent, const Vector3 target)
    {
        m_Impl->RequireOwner("SetAgentTarget");
        if (!Math::IsFinite(target))
            throw std::invalid_argument("Navigation agent target contains non-finite values.");
        std::scoped_lock lock(m_Impl->State->Mutex);
        const auto found = m_Impl->State->Agents.find(agent);
        if (found == m_Impl->State->Agents.end())
            return false;
        found->second.State.Target = target;
        found->second.State.HasTarget = true;
        found->second.State.Status = NavigationAgentStatus::Seeking;
        found->second.Path.clear();
        found->second.NextPoint = 0;
        return true;
    }

    std::vector<NavigationAgentState> NavigationWorld::StepCrowd(const float deltaSeconds)
    {
        m_Impl->RequireOwner("StepCrowd");
        if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0F || deltaSeconds > 1.0F)
            throw std::invalid_argument("Navigation crowd delta time is invalid.");
        std::scoped_lock lock(m_Impl->State->Mutex);
        if (!m_Impl->State->Mesh)
            throw std::logic_error("Navigation world has no published mesh.");
        std::vector<NavigationObstacle> obstacles;
        for (const auto& [id, obstacle] : m_Impl->State->Obstacles)
        {
            (void)id;
            obstacles.push_back(obstacle);
        }
        for (auto& [id, agent] : m_Impl->State->Agents)
        {
            (void)id;
            if (!agent.State.HasTarget)
                continue;
            if (agent.Path.empty())
            {
                const auto path =
                    SolvePath(*m_Impl->State->Mesh,
                              {agent.State.Position, agent.State.Target, agent.Specification.AreaMask}, {}, obstacles);
                if (path.State != NavigationPathState::Succeeded)
                {
                    agent.State.Status = NavigationAgentStatus::Unreachable;
                    agent.State.Velocity = {};
                    continue;
                }
                agent.Path = path.Points;
                agent.NextPoint = std::min<std::size_t>(1, agent.Path.size());
            }
            while (agent.NextPoint < agent.Path.size() &&
                   DistanceSquared(agent.State.Position, agent.Path[agent.NextPoint]) <=
                       agent.Specification.Radius * agent.Specification.Radius)
                ++agent.NextPoint;
            if (agent.NextPoint >= agent.Path.size())
            {
                agent.State.Position = agent.State.Target;
                agent.State.Velocity = {};
                agent.State.HasTarget = false;
                agent.State.Status = NavigationAgentStatus::Arrived;
                continue;
            }
            const auto target = agent.Path[agent.NextPoint];
            const Vector3 delta{target.X - agent.State.Position.X, target.Y - agent.State.Position.Y,
                                target.Z - agent.State.Position.Z};
            const auto distance = std::sqrt(DistanceSquared(target, agent.State.Position));
            Vector3 desired{delta.X / distance * agent.Specification.MaximumSpeed,
                            delta.Y / distance * agent.Specification.MaximumSpeed,
                            delta.Z / distance * agent.Specification.MaximumSpeed};
            for (const auto& [otherId, other] : m_Impl->State->Agents)
            {
                if (otherId == agent.State.Id)
                    continue;
                const Vector3 separation{agent.State.Position.X - other.State.Position.X,
                                         agent.State.Position.Y - other.State.Position.Y,
                                         agent.State.Position.Z - other.State.Position.Z};
                const auto separationDistance = VectorLength(separation);
                const auto required = agent.Specification.Radius + other.Specification.Radius;
                if (separationDistance > 1.0e-5F && separationDistance < required * 2.0F)
                {
                    const auto strength = (required * 2.0F - separationDistance) / (required * 2.0F);
                    desired.X += separation.X / separationDistance * strength * agent.Specification.MaximumSpeed;
                    desired.Z += separation.Z / separationDistance * strength * agent.Specification.MaximumSpeed;
                }
            }
            const auto maximumChange = agent.Specification.MaximumAcceleration * deltaSeconds;
            const Vector3 velocityDelta{desired.X - agent.State.Velocity.X, desired.Y - agent.State.Velocity.Y,
                                        desired.Z - agent.State.Velocity.Z};
            const auto velocityChange = VectorLength(velocityDelta);
            const auto scale = velocityChange > maximumChange ? maximumChange / velocityChange : 1.0F;
            agent.State.Velocity.X += velocityDelta.X * scale;
            agent.State.Velocity.Y += velocityDelta.Y * scale;
            agent.State.Velocity.Z += velocityDelta.Z * scale;
            agent.State.Position.X += agent.State.Velocity.X * deltaSeconds;
            agent.State.Position.Y += agent.State.Velocity.Y * deltaSeconds;
            agent.State.Position.Z += agent.State.Velocity.Z * deltaSeconds;
            agent.State.Status = NavigationAgentStatus::Seeking;
        }
        std::vector<NavigationAgentState> result;
        result.reserve(m_Impl->State->Agents.size());
        for (const auto& [id, agent] : m_Impl->State->Agents)
        {
            (void)id;
            result.push_back(agent.State);
        }
        return result;
    }

    std::vector<NavigationAgentState> NavigationWorld::Agents() const
    {
        m_Impl->RequireOwner("Agents");
        std::scoped_lock lock(m_Impl->State->Mutex);
        std::vector<NavigationAgentState> result;
        result.reserve(m_Impl->State->Agents.size());
        for (const auto& [id, agent] : m_Impl->State->Agents)
        {
            (void)id;
            result.push_back(agent.State);
        }
        return result;
    }

    NavigationObstacleId NavigationWorld::AddObstacle(const NavigationObstacle& obstacle)
    {
        m_Impl->RequireOwner("AddObstacle");
        if (!Math::IsFinite(obstacle.Position) || !std::isfinite(obstacle.Radius) || obstacle.Radius <= 0.0F)
            throw std::invalid_argument("Navigation obstacle is invalid.");
        std::scoped_lock lock(m_Impl->State->Mutex);
        const NavigationObstacleId id(m_Impl->State->NextObstacle++);
        m_Impl->State->Obstacles.emplace(id, obstacle);
        m_Impl->State->Generation.fetch_add(1, std::memory_order_release);
        for (auto& [agentId, agent] : m_Impl->State->Agents)
        {
            (void)agentId;
            agent.Path.clear();
        }
        return id;
    }

    bool NavigationWorld::UpdateObstacle(const NavigationObstacleId obstacle, const NavigationObstacle& value)
    {
        m_Impl->RequireOwner("UpdateObstacle");
        if (!Math::IsFinite(value.Position) || !std::isfinite(value.Radius) || value.Radius <= 0.0F)
            throw std::invalid_argument("Navigation obstacle is invalid.");
        std::scoped_lock lock(m_Impl->State->Mutex);
        const auto found = m_Impl->State->Obstacles.find(obstacle);
        if (found == m_Impl->State->Obstacles.end())
            return false;
        found->second = value;
        m_Impl->State->Generation.fetch_add(1, std::memory_order_release);
        for (auto& [agentId, agent] : m_Impl->State->Agents)
        {
            (void)agentId;
            agent.Path.clear();
        }
        return true;
    }

    bool NavigationWorld::RemoveObstacle(const NavigationObstacleId obstacle)
    {
        m_Impl->RequireOwner("RemoveObstacle");
        std::scoped_lock lock(m_Impl->State->Mutex);
        if (m_Impl->State->Obstacles.erase(obstacle) == 0)
            return false;
        m_Impl->State->Generation.fetch_add(1, std::memory_order_release);
        for (auto& [agentId, agent] : m_Impl->State->Agents)
        {
            (void)agentId;
            agent.Path.clear();
        }
        return true;
    }

    std::uint64_t NavigationWorld::MeshRevision() const noexcept
    {
        return m_Impl->State->Revision.load(std::memory_order_acquire);
    }
    void NavigationWorld::Close()
    {
        if (std::this_thread::get_id() != m_Impl->Owner)
            throw std::logic_error("NavigationWorld::Close must run on the owner thread.");
        m_Impl->State->Open.store(false, std::memory_order_release);
        std::scoped_lock lock(m_Impl->State->Mutex);
        m_Impl->State->Mesh.reset();
        m_Impl->State->Agents.clear();
        m_Impl->State->Obstacles.clear();
        m_Impl->State->Generation.fetch_add(1, std::memory_order_release);
    }

    class NavigationSystem::Impl final
    {
      public:
        Impl(const NavigationSystemSpecification& specification, Ref<JobSystem> jobs)
            : Owner(std::this_thread::get_id())
        {
            Service->MaximumWorlds = specification.MaximumWorlds;
            Service->MaximumAgentsPerWorld = specification.MaximumAgentsPerWorld;
            Service->MaximumAsyncQueries = specification.MaximumAsyncQueries;
            if (!jobs)
            {
                JobSystemSpecification jobSpecification;
                jobSpecification.WorkerCount = 2;
                jobSpecification.BlockingWorkerCount = 1;
                jobs = CreateRef<JobSystem>(jobSpecification);
                Service->OwnScheduler = true;
            }
            Service->Scheduler = std::move(jobs);
            Service->Scope = Service->Scheduler->CreateScope("Navigation");
        }
        std::thread::id Owner;
        std::shared_ptr<NavigationServiceState> Service = std::make_shared<NavigationServiceState>();
    };

    NavigationSystem::NavigationSystem(const NavigationSystemSpecification specification, Ref<JobSystem> jobs)
        : m_Impl(std::make_unique<Impl>(specification, std::move(jobs)))
    {
        if (specification.Mode == NavigationMode::Disabled || specification.MaximumWorlds == 0 ||
            specification.MaximumWorlds > 4096 || specification.MaximumAgentsPerWorld == 0 ||
            specification.MaximumAgentsPerWorld > 65536 || specification.MaximumAsyncQueries == 0 ||
            specification.MaximumAsyncQueries > 65536)
            throw std::invalid_argument("NavigationSystem specification is invalid.");
    }
    NavigationSystem::~NavigationSystem() = default;
    bool NavigationSystem::IsOpen() const noexcept { return m_Impl->Service->Open.load(std::memory_order_acquire); }
    Ref<NavigationWorld> NavigationSystem::CreateWorld()
    {
        if (std::this_thread::get_id() != m_Impl->Owner)
            throw std::logic_error("NavigationSystem::CreateWorld must run on the owner thread.");
        if (!IsOpen())
            throw std::logic_error("NavigationSystem is closed.");
        const auto count = m_Impl->Service->Worlds.fetch_add(1, std::memory_order_relaxed) + 1;
        if (count > m_Impl->Service->MaximumWorlds)
        {
            m_Impl->Service->Worlds.fetch_sub(1, std::memory_order_relaxed);
            throw std::runtime_error("NavigationSystem world capacity was exhausted.");
        }
        return CreateRef<NavigationWorld>(
            std::make_unique<NavigationWorld::Impl>(m_Impl->Service, std::make_shared<NavigationWorldState>()));
    }
    void NavigationSystem::Close()
    {
        if (std::this_thread::get_id() != m_Impl->Owner)
            throw std::logic_error("NavigationSystem::Close must run on the owner thread.");
        m_Impl->Service->Open.store(false, std::memory_order_release);
        if (m_Impl->Service->Scope)
        {
            m_Impl->Service->Scope->Cancel();
            m_Impl->Service->Scope->Wait();
            m_Impl->Service->Scope.Reset();
        }
        if (m_Impl->Service->OwnScheduler && m_Impl->Service->Scheduler)
            m_Impl->Service->Scheduler->Close();
        m_Impl->Service->Scheduler.Reset();
    }
} // namespace Keire
