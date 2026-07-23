#include "Keire/Assets/AssetSystem.h"

#include "Keire/Log.h"

#include "KeireInternal/Assets/AssetInternal.h"

#include <zstd.h>

#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <fstream>
#include <functional>
#include <limits>
#include <mutex>
#include <ranges>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <variant>

namespace Keire
{
    namespace
    {
        [[nodiscard]] std::size_t DefaultWorkerCount() noexcept
        {
            const auto available = std::thread::hardware_concurrency();
            return std::clamp<std::size_t>(available > 1 ? available - 1U : 1U, 1U, 16U);
        }

        [[nodiscard]] bool IsValidUtf8(const std::span<const std::byte> bytes) noexcept
        {
            std::size_t index = 0;
            while (index < bytes.size())
            {
                const auto first = std::to_integer<unsigned char>(bytes[index]);
                std::size_t continuation = 0;
                std::uint32_t value = 0;
                if (first <= 0x7fU)
                {
                    ++index;
                    continue;
                }
                if ((first & 0xe0U) == 0xc0U)
                {
                    continuation = 1;
                    value = first & 0x1fU;
                }
                else if ((first & 0xf0U) == 0xe0U)
                {
                    continuation = 2;
                    value = first & 0x0fU;
                }
                else if ((first & 0xf8U) == 0xf0U)
                {
                    continuation = 3;
                    value = first & 0x07U;
                }
                else
                {
                    return false;
                }
                if (index + continuation >= bytes.size())
                    return false;
                for (std::size_t offset = 1; offset <= continuation; ++offset)
                {
                    const auto byte = std::to_integer<unsigned char>(bytes[index + offset]);
                    if ((byte & 0xc0U) != 0x80U)
                        return false;
                    value = (value << 6U) | (byte & 0x3fU);
                }
                if ((continuation == 1 && value < 0x80U) || (continuation == 2 && value < 0x800U) ||
                    (continuation == 3 && value < 0x10000U) || value > 0x10ffffU ||
                    (value >= 0xd800U && value <= 0xdfffU))
                    return false;
                index += continuation + 1U;
            }
            return true;
        }

        [[nodiscard]] std::uint64_t ThreadHash() noexcept
        {
            return static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
        }
    } // namespace

    class AssetStreamOperation::Impl final
    {
      public:
        mutable std::mutex Mutex;
        mutable std::condition_variable Changed;
        AssetStreamState Current = AssetStreamState::Queued;
        std::vector<std::byte> Bytes;
        AssetDiagnostic Failure;
        bool CancellationRequested = false;
    };

    AssetStreamOperation::AssetStreamOperation() : m_Impl(std::make_unique<Impl>()) {}
    AssetStreamOperation::~AssetStreamOperation() = default;

    AssetStreamState AssetStreamOperation::State() const noexcept
    {
        std::scoped_lock lock(m_Impl->Mutex);
        return m_Impl->Current;
    }

    bool AssetStreamOperation::Wait(const std::chrono::milliseconds timeout) const
    {
        if (timeout.count() < 0)
            throw std::invalid_argument("Asset stream wait timeout cannot be negative.");
        std::unique_lock lock(m_Impl->Mutex);
        return m_Impl->Changed.wait_for(lock, timeout,
                                        [this]
                                        {
                                            return m_Impl->Current == AssetStreamState::Succeeded ||
                                                   m_Impl->Current == AssetStreamState::Failed ||
                                                   m_Impl->Current == AssetStreamState::Cancelled;
                                        });
    }

    std::vector<std::byte> AssetStreamOperation::Result() const
    {
        std::scoped_lock lock(m_Impl->Mutex);
        if (m_Impl->Current != AssetStreamState::Succeeded)
            throw std::logic_error("Asset stream result is unavailable.");
        return m_Impl->Bytes;
    }

    AssetDiagnostic AssetStreamOperation::Diagnostic() const
    {
        std::scoped_lock lock(m_Impl->Mutex);
        return m_Impl->Failure;
    }

    void AssetStreamOperation::Cancel() noexcept
    {
        {
            std::scoped_lock lock(m_Impl->Mutex);
            m_Impl->CancellationRequested = true;
            if (m_Impl->Current == AssetStreamState::Queued)
                m_Impl->Current = AssetStreamState::Cancelled;
        }
        m_Impl->Changed.notify_all();
    }

    class AssetSystem::Impl final
    {
      public:
        struct MountRecord
        {
            AssetMountSpecification Specification;
            Detail::CatalogData Catalog;
        };

        struct ResolvedEntry
        {
            Detail::CatalogEntry Entry;
            int Priority = 0;
        };

        struct Job
        {
            Ref<Detail::AssetHandleState> State;
            ResolvedEntry Source;
            AssetDecoderRegistration Decoder;
            bool Reload = false;
            std::uint64_t Sequence = 0;
        };

        struct Completion
        {
            Ref<Detail::AssetHandleState> State;
            Ref<Asset> Value;
            AssetDiagnostic Diagnostic;
            bool Reload = false;
            bool CountedInQueue = true;
        };

        struct StreamJob
        {
            Ref<AssetStreamOperation> Operation;
            ResolvedEntry Source;
            std::uint64_t Offset = 0;
            std::size_t Bytes = 0;
        };

        using WorkerJob = std::variant<Job, StreamJob>;

        explicit Impl(AssetSystemSpecification value, Ref<EventBus> events)
            : Specification(std::move(value)), Events(std::move(events)), OwnerThread(std::this_thread::get_id())
        {
            if (Specification.QueueCapacity == 0)
                throw std::invalid_argument("Asset queue capacity must be greater than zero.");
            if (Specification.MaximumAssetBytes == 0 || Specification.MaximumStreamReadBytes == 0)
                throw std::invalid_argument("Maximum asset size must be greater than zero.");
            if (Specification.WorkerCount == 0)
                Specification.WorkerCount = DefaultWorkerCount();
            if (Specification.WorkerCount > 16)
                throw std::invalid_argument("Asset worker count must be in the range 1..16.");

            RegisterBuiltinDecoders();
            for (auto& decoder : Specification.Decoders)
                RegisterDecoder(std::move(decoder));
        }

        void RegisterBuiltinDecoders()
        {
            RegisterDecoder({BinaryAsset::StaticType(), CreateRef<BinaryAsset>(),
                             [](const std::span<const std::byte> bytes) -> Ref<Asset>
                             { return CreateRef<BinaryAsset>(std::vector<std::byte>(bytes.begin(), bytes.end())); }});
            RegisterDecoder({TextAsset::StaticType(), CreateRef<TextAsset>(),
                             [](const std::span<const std::byte> bytes) -> Ref<Asset>
                             {
                                 auto content = bytes;
                                 if (content.size() >= 3 && content[0] == std::byte{0xef} &&
                                     content[1] == std::byte{0xbb} && content[2] == std::byte{0xbf})
                                     content = content.subspan(3);
                                 if (!IsValidUtf8(content))
                                     throw std::runtime_error("Text asset is not valid UTF-8.");
                                 if (content.empty())
                                     return CreateRef<TextAsset>();
                                 return CreateRef<TextAsset>(
                                     std::string(reinterpret_cast<const char*>(content.data()), content.size()));
                             }});
        }

        void RegisterDecoder(AssetDecoderRegistration decoder)
        {
            if (!decoder.Type || !decoder.Fallback || !decoder.Decode || decoder.Fallback->Type() != decoder.Type)
                throw std::invalid_argument("Asset decoder registration is incomplete or has a mismatched fallback.");
            if (!Decoders.emplace(decoder.Type, std::move(decoder)).second)
                throw std::invalid_argument("An asset decoder is already registered for this type.");
        }

        void RequireOwnerThread(const char* operation) const
        {
            if (std::this_thread::get_id() != OwnerThread)
                throw std::logic_error(std::string("AssetSystem::") + operation + " must run on its owner thread.");
        }

        void StartWorkers()
        {
            Workers.reserve(Specification.WorkerCount);
            for (std::size_t index = 0; index < Specification.WorkerCount; ++index)
                Workers.emplace_back([this] { WorkerMain(); });
        }

        [[nodiscard]] bool HasJobs() const noexcept
        {
            return std::ranges::any_of(Jobs, [](const auto& queue) { return !queue.empty(); }) || !StreamJobs.empty();
        }

        [[nodiscard]] WorkerJob PopJob()
        {
            for (auto& queue : Jobs)
            {
                if (!queue.empty())
                {
                    Job job = std::move(queue.front());
                    queue.pop_front();
                    return job;
                }
            }
            if (!StreamJobs.empty())
            {
                StreamJob job = std::move(StreamJobs.front());
                StreamJobs.pop_front();
                return job;
            }
            throw std::logic_error("Asset worker woke without a queued job.");
        }

        void WorkerMain() noexcept
        {
            for (;;)
            {
                WorkerJob job;
                {
                    std::unique_lock lock(Mutex);
                    WorkAvailable.wait(lock, [this] { return Stopping || HasJobs(); });
                    if (Stopping && !HasJobs())
                        return;
                    job = PopJob();
                }

                if (auto* assetJob = std::get_if<Job>(&job))
                {
                    assetJob->State->SetLoading(assetJob->Reload);
                    Completion completion{assetJob->State, {}, {}, assetJob->Reload, true};
                    try
                    {
                        completion.Value = LoadValue(*assetJob);
                    }
                    catch (const std::exception& error)
                    {
                        completion.Diagnostic = {"load", error.what()};
                    }
                    catch (...)
                    {
                        completion.Diagnostic = {"load", "The asset decoder reported an unknown failure."};
                    }
                    {
                        std::scoped_lock lock(Mutex);
                        Completions.push_back(std::move(completion));
                    }
                }
                else
                {
                    RunStreamJob(std::get<StreamJob>(job));
                    std::scoped_lock lock(Mutex);
                    if (QueueSize > 0)
                        --QueueSize;
                }
            }
        }

        [[nodiscard]] std::vector<std::byte> DecodeEntry(const Detail::CatalogEntry& entry) const
        {
            if (entry.UncompressedBytes > Specification.MaximumAssetBytes ||
                entry.UncompressedBytes > std::numeric_limits<std::size_t>::max() ||
                entry.CompressedBytes > std::numeric_limits<std::size_t>::max())
                throw std::runtime_error("Asset exceeds the configured maximum size.");
            std::ifstream stream(entry.PackPath, std::ios::binary);
            if (!stream)
                throw std::runtime_error("Could not open asset pack: " + entry.PackPath.string());
            Detail::ValidatePackHeader(stream, entry.PackPath);
            std::vector<std::byte> decoded(static_cast<std::size_t>(entry.UncompressedBytes));
            if (entry.Pages.empty())
            {
                stream.seekg(static_cast<std::streamoff>(entry.Offset), std::ios::beg);
                std::vector<std::byte> compressed(static_cast<std::size_t>(entry.CompressedBytes));
                if (!compressed.empty() && !stream.read(reinterpret_cast<char*>(compressed.data()),
                                                        static_cast<std::streamsize>(compressed.size())))
                    throw std::runtime_error("Could not read the complete asset payload.");
                const auto result =
                    ZSTD_decompress(decoded.data(), decoded.size(), compressed.data(), compressed.size());
                if (ZSTD_isError(result) || result != decoded.size())
                    throw std::runtime_error(std::string("Zstandard decompression failed: ") +
                                             ZSTD_getErrorName(result));
            }
            else
            {
                for (const auto& page : entry.Pages)
                {
                    stream.seekg(static_cast<std::streamoff>(page.Offset), std::ios::beg);
                    std::vector<std::byte> compressed(static_cast<std::size_t>(page.CompressedBytes));
                    if (!stream.read(reinterpret_cast<char*>(compressed.data()),
                                     static_cast<std::streamsize>(compressed.size())))
                        throw std::runtime_error("Could not read a complete asset stream page.");
                    auto destination = std::span(decoded).subspan(static_cast<std::size_t>(page.UncompressedOffset),
                                                                  static_cast<std::size_t>(page.UncompressedBytes));
                    const auto result =
                        ZSTD_decompress(destination.data(), destination.size(), compressed.data(), compressed.size());
                    if (ZSTD_isError(result) || result != destination.size() ||
                        Detail::Sha256(destination) != page.Digest)
                        throw std::runtime_error("Asset stream page failed decompression or integrity validation.");
                }
            }
            if (Detail::Sha256(decoded) != entry.Digest)
                throw std::runtime_error("Asset payload failed its SHA-256 integrity check.");
            return decoded;
        }

        [[nodiscard]] Ref<Asset> LoadValue(const Job& job) const
        {
            const auto& entry = job.Source.Entry;
            auto decoded = DecodeEntry(entry);

            Ref<Asset> asset = job.Decoder.Decode(decoded);
            if (!asset || asset->Type() != entry.Type)
                throw std::runtime_error("Asset decoder returned an empty or mismatched asset type.");
            return asset;
        }

        void RunStreamJob(const StreamJob& job) noexcept
        {
            {
                std::scoped_lock lock(job.Operation->m_Impl->Mutex);
                if (job.Operation->m_Impl->CancellationRequested)
                {
                    job.Operation->m_Impl->Current = AssetStreamState::Cancelled;
                    job.Operation->m_Impl->Changed.notify_all();
                    return;
                }
                job.Operation->m_Impl->Current = AssetStreamState::Reading;
            }
            job.Operation->m_Impl->Changed.notify_all();
            try
            {
                const auto& entry = job.Source.Entry;
                std::vector<std::byte> result(job.Bytes);
                if (job.Bytes != 0)
                {
                    if (entry.Pages.empty())
                    {
                        const auto all = DecodeEntry(entry);
                        std::ranges::copy(std::span(all).subspan(static_cast<std::size_t>(job.Offset), job.Bytes),
                                          result.begin());
                    }
                    else
                    {
                        std::ifstream stream(entry.PackPath, std::ios::binary);
                        if (!stream)
                            throw std::runtime_error("Could not open asset pack for a range read.");
                        Detail::ValidatePackHeader(stream, entry.PackPath);
                        const auto requestEnd = job.Offset + job.Bytes;
                        for (const auto& page : entry.Pages)
                        {
                            const auto pageEnd = page.UncompressedOffset + page.UncompressedBytes;
                            if (pageEnd <= job.Offset || page.UncompressedOffset >= requestEnd)
                                continue;
                            {
                                std::scoped_lock lock(job.Operation->m_Impl->Mutex);
                                if (job.Operation->m_Impl->CancellationRequested)
                                    throw AssetStreamState::Cancelled;
                            }
                            stream.seekg(static_cast<std::streamoff>(page.Offset), std::ios::beg);
                            std::vector<std::byte> compressed(static_cast<std::size_t>(page.CompressedBytes));
                            if (!stream.read(reinterpret_cast<char*>(compressed.data()),
                                             static_cast<std::streamsize>(compressed.size())))
                                throw std::runtime_error("Could not read a complete streamed asset page.");
                            std::vector<std::byte> decoded(static_cast<std::size_t>(page.UncompressedBytes));
                            const auto decodedBytes =
                                ZSTD_decompress(decoded.data(), decoded.size(), compressed.data(), compressed.size());
                            if (ZSTD_isError(decodedBytes) || decodedBytes != decoded.size() ||
                                Detail::Sha256(decoded) != page.Digest)
                                throw std::runtime_error("Streamed asset page failed integrity validation.");
                            const auto copyBegin = std::max(job.Offset, page.UncompressedOffset);
                            const auto copyEnd = std::min(requestEnd, pageEnd);
                            const auto sourceOffset = static_cast<std::size_t>(copyBegin - page.UncompressedOffset);
                            const auto destinationOffset = static_cast<std::size_t>(copyBegin - job.Offset);
                            const auto copyBytes = static_cast<std::size_t>(copyEnd - copyBegin);
                            std::ranges::copy(std::span(decoded).subspan(sourceOffset, copyBytes),
                                              result.begin() + static_cast<std::ptrdiff_t>(destinationOffset));
                        }
                    }
                }
                {
                    std::scoped_lock lock(job.Operation->m_Impl->Mutex);
                    if (job.Operation->m_Impl->CancellationRequested)
                        job.Operation->m_Impl->Current = AssetStreamState::Cancelled;
                    else
                    {
                        job.Operation->m_Impl->Bytes = std::move(result);
                        job.Operation->m_Impl->Current = AssetStreamState::Succeeded;
                    }
                }
            }
            catch (const AssetStreamState)
            {
                std::scoped_lock lock(job.Operation->m_Impl->Mutex);
                job.Operation->m_Impl->Current = AssetStreamState::Cancelled;
            }
            catch (const std::exception& exception)
            {
                std::scoped_lock lock(job.Operation->m_Impl->Mutex);
                job.Operation->m_Impl->Failure = {"stream", exception.what()};
                job.Operation->m_Impl->Current = AssetStreamState::Failed;
            }
            catch (...)
            {
                std::scoped_lock lock(job.Operation->m_Impl->Mutex);
                job.Operation->m_Impl->Failure = {"stream", "The streamed asset read reported an unknown failure."};
                job.Operation->m_Impl->Current = AssetStreamState::Failed;
            }
            job.Operation->m_Impl->Changed.notify_all();
        }

        AssetSystemSpecification Specification;
        Ref<EventBus> Events;
        std::thread::id OwnerThread;
        std::unordered_map<AssetTypeId, AssetDecoderRegistration> Decoders;
        std::vector<MountRecord> Mounts;
        std::unordered_map<AssetId, ResolvedEntry> Resolved;
        std::unordered_map<AssetId, Ref<Detail::AssetHandleState>> States;
        std::array<std::deque<Job>, 5> Jobs;
        std::deque<StreamJob> StreamJobs;
        std::vector<Ref<AssetStreamOperation>> StreamOperations;
        std::deque<Completion> Completions;
        std::vector<std::thread> Workers;
        mutable std::mutex Mutex;
        std::condition_variable WorkAvailable;
        bool Open = true;
        bool Stopping = false;
        std::size_t QueueSize = 0;
        std::size_t QueueHighWaterMark = 0;
        std::uint64_t Sequence = 0;
        std::uint64_t CompletedLoads = 0;
        std::uint64_t FailedLoads = 0;
        std::uint64_t Reloads = 0;
        std::uint64_t Evictions = 0;
    };

    AssetSystem::AssetSystem(AssetSystemSpecification specification, Ref<EventBus> events)
        : m_Impl(std::make_unique<Impl>(std::move(specification), std::move(events)))
    {
        try
        {
            if (m_Impl->Specification.Mode == AssetMode::Development &&
                std::filesystem::exists(m_Impl->Specification.DevelopmentCatalog))
                Mount({m_Impl->Specification.DevelopmentCatalog, 0, true});
            for (const auto& mount : m_Impl->Specification.Mounts)
                Mount(mount);
            if (m_Impl->Specification.Mode == AssetMode::Cooked && m_Impl->Mounts.empty())
                throw std::invalid_argument("Cooked asset mode requires at least one catalog mount.");
            m_Impl->StartWorkers();
        }
        catch (...)
        {
            Close();
            throw;
        }
    }

    AssetSystem::~AssetSystem() { Close(); }

    Ref<Detail::AssetHandleState> AssetSystem::LoadErased(const AssetId id, const AssetTypeId type,
                                                          const AssetPriority priority)
    {
        std::scoped_lock lock(m_Impl->Mutex);
        if (!m_Impl->Open || m_Impl->Specification.Mode == AssetMode::Disabled)
            throw std::logic_error("Asset loading is unavailable because the asset system is disabled or closed.");
        const auto decoder = m_Impl->Decoders.find(type);
        if (decoder == m_Impl->Decoders.end())
            throw std::invalid_argument("No decoder is registered for the requested asset type.");
        if (const auto existing = m_Impl->States.find(id); existing != m_Impl->States.end())
        {
            if (existing->second->Type() != type)
                throw std::invalid_argument("Asset was requested through a handle with a different type.");
            return existing->second;
        }

        auto state = CreateRef<Detail::AssetHandleState>(id, type, decoder->second.Fallback, ThreadHash());
        m_Impl->States.emplace(id, state);
        const auto entry = m_Impl->Resolved.find(id);
        if (entry == m_Impl->Resolved.end() || entry->second.Entry.Type != type)
        {
            m_Impl->Completions.push_back({state,
                                           {},
                                           {"resolve", entry == m_Impl->Resolved.end()
                                                           ? "Asset ID is not present in any mounted catalog."
                                                           : "Catalog type does not match the requested handle type."},
                                           false,
                                           false});
            return state;
        }
        if (m_Impl->QueueSize >= m_Impl->Specification.QueueCapacity)
        {
            m_Impl->Completions.push_back(
                {state, {}, {"queue", "Asset loading queue capacity was exhausted."}, false, false});
            return state;
        }

        std::function<void(AssetId)> queueDependency = [&](const AssetId dependencyId)
        {
            if (m_Impl->States.contains(dependencyId) || m_Impl->QueueSize + 1U >= m_Impl->Specification.QueueCapacity)
                return;
            const auto dependency = m_Impl->Resolved.find(dependencyId);
            if (dependency == m_Impl->Resolved.end())
                return;
            const auto dependencyDecoder = m_Impl->Decoders.find(dependency->second.Entry.Type);
            if (dependencyDecoder == m_Impl->Decoders.end())
                return;
            auto dependencyState = CreateRef<Detail::AssetHandleState>(
                dependencyId, dependency->second.Entry.Type, dependencyDecoder->second.Fallback, ThreadHash());
            m_Impl->States.emplace(dependencyId, dependencyState);
            try
            {
                for (const auto nested : dependency->second.Entry.Dependencies)
                    queueDependency(nested);
                m_Impl->Jobs[static_cast<std::size_t>(priority)].push_back(
                    {dependencyState, dependency->second, dependencyDecoder->second, false, ++m_Impl->Sequence});
            }
            catch (...)
            {
                m_Impl->States.erase(dependencyId);
                throw;
            }
            ++m_Impl->QueueSize;
        };
        for (const auto dependency : entry->second.Entry.Dependencies)
            queueDependency(dependency);

        auto& queue = m_Impl->Jobs[static_cast<std::size_t>(priority)];
        try
        {
            queue.push_back({state, entry->second, decoder->second, false, ++m_Impl->Sequence});
        }
        catch (...)
        {
            m_Impl->States.erase(id);
            throw;
        }
        ++m_Impl->QueueSize;
        m_Impl->QueueHighWaterMark = std::max(m_Impl->QueueHighWaterMark, m_Impl->QueueSize);
        m_Impl->WorkAvailable.notify_one();
        return state;
    }

    void AssetSystem::Mount(const AssetMountSpecification& specification)
    {
        m_Impl->RequireOwnerThread("Mount");
        if (specification.CatalogPath.empty())
            throw std::invalid_argument("Asset catalog path must not be empty.");
        auto catalog = Detail::LoadCatalog(specification.CatalogPath);
        std::unordered_set<AssetId> catalogIds;
        std::unordered_set<std::filesystem::path> packs;
        for (const auto& entry : catalog.Entries)
        {
            if (!catalogIds.insert(entry.Id).second)
                throw std::runtime_error("Asset catalog contains a duplicate asset ID: " + entry.Id.ToString());
            if (entry.Offset < Detail::PackHeaderBytes || entry.CompressedBytes == 0 ||
                entry.UncompressedBytes > m_Impl->Specification.MaximumAssetBytes ||
                entry.Offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()))
                throw std::runtime_error("Asset catalog contains an invalid payload range.");
            if (packs.insert(entry.PackPath).second)
            {
                std::ifstream stream(entry.PackPath, std::ios::binary);
                if (!stream)
                    throw std::runtime_error("Could not open asset pack: " + entry.PackPath.string());
                Detail::ValidatePackHeader(stream, entry.PackPath);
            }
            std::error_code error;
            const auto size = std::filesystem::file_size(entry.PackPath, error);
            if (error || entry.Offset > size || entry.CompressedBytes > size - entry.Offset)
                throw std::runtime_error("Asset catalog payload extends beyond its pack file.");
        }

        const auto canonical = catalog.Path;
        if (std::ranges::any_of(m_Impl->Mounts,
                                [&canonical](const auto& mount) { return mount.Catalog.Path == canonical; }))
            throw std::invalid_argument("Asset catalog is already mounted.");

        auto mounts = m_Impl->Mounts;
        mounts.push_back({specification, std::move(catalog)});
        std::ranges::sort(mounts, [](const auto& left, const auto& right)
                          { return left.Specification.Priority < right.Specification.Priority; });
        std::unordered_map<AssetId, Impl::ResolvedEntry> resolved;
        for (const auto& mount : mounts)
        {
            for (const auto& entry : mount.Catalog.Entries)
            {
                const auto current = resolved.find(entry.Id);
                if (current != resolved.end())
                {
                    if (current->second.Priority == mount.Specification.Priority)
                        throw std::runtime_error("Duplicate asset IDs may not share a mount priority: " +
                                                 entry.Id.ToString());
                    if (!mount.Specification.AllowOverrides)
                        throw std::runtime_error("Asset catalog override was not explicitly permitted for " +
                                                 entry.Id.ToString());
                }
                resolved[entry.Id] = {entry, mount.Specification.Priority};
            }
        }
        for (const auto& [id, entry] : resolved)
        {
            for (const auto dependency : entry.Entry.Dependencies)
            {
                if (!resolved.contains(dependency))
                    throw std::runtime_error("Asset dependency is not present in the resolved catalog: " +
                                             dependency.ToString());
            }
        }
        std::unordered_map<AssetId, std::uint8_t> marks;
        std::function<void(AssetId)> visit = [&](const AssetId id)
        {
            if (marks[id] == 1)
                throw std::runtime_error("Asset catalog dependency graph contains a cycle.");
            if (marks[id] == 2)
                return;
            marks[id] = 1;
            for (const auto dependency : resolved.at(id).Entry.Dependencies)
                visit(dependency);
            marks[id] = 2;
        };
        for (const auto& [id, entry] : resolved)
            visit(id);
        std::scoped_lock lock(m_Impl->Mutex);
        if (!m_Impl->Open)
            throw std::logic_error("Cannot mount a catalog after the asset system has closed.");
        m_Impl->Mounts = std::move(mounts);
        m_Impl->Resolved = std::move(resolved);

        bool queuedRecovery = false;
        for (const auto& [id, state] : m_Impl->States)
        {
            if (m_Impl->QueueSize >= m_Impl->Specification.QueueCapacity)
                break;
            const auto entry = m_Impl->Resolved.find(id);
            const auto decoder = m_Impl->Decoders.find(state->Type());
            if (entry == m_Impl->Resolved.end() || decoder == m_Impl->Decoders.end() ||
                entry->second.Entry.Type != state->Type())
                continue;

            auto pendingResolve = m_Impl->Completions.end();
            const auto current = state->State();
            if (current == AssetState::Queued)
            {
                pendingResolve = std::ranges::find_if(m_Impl->Completions,
                                                      [&state](const Impl::Completion& completion)
                                                      {
                                                          return completion.State.Get() == state.Get() &&
                                                                 !completion.CountedInQueue &&
                                                                 completion.Diagnostic.Operation == "resolve";
                                                      });
                if (pendingResolve == m_Impl->Completions.end())
                    continue;
            }
            else if (current != AssetState::Failed || state->Diagnostic().Operation != "resolve")
            {
                continue;
            }

            m_Impl->Jobs[static_cast<std::size_t>(AssetPriority::Normal)].push_back(
                {state, entry->second, decoder->second, false, ++m_Impl->Sequence});
            if (pendingResolve != m_Impl->Completions.end())
                m_Impl->Completions.erase(pendingResolve);
            state->SetLoading(false);
            ++m_Impl->QueueSize;
            m_Impl->QueueHighWaterMark = std::max(m_Impl->QueueHighWaterMark, m_Impl->QueueSize);
            queuedRecovery = true;
        }
        if (queuedRecovery)
            m_Impl->WorkAvailable.notify_all();
    }

    bool AssetSystem::Unmount(const std::filesystem::path& catalogPath)
    {
        m_Impl->RequireOwnerThread("Unmount");
        const auto canonical = std::filesystem::absolute(catalogPath).lexically_normal();
        const auto iterator = std::ranges::find_if(m_Impl->Mounts, [&canonical](const auto& mount)
                                                   { return mount.Catalog.Path == canonical; });
        if (iterator == m_Impl->Mounts.end())
            return false;
        auto mounts = m_Impl->Mounts;
        mounts.erase(mounts.begin() + std::distance(m_Impl->Mounts.begin(), iterator));
        std::unordered_map<AssetId, Impl::ResolvedEntry> resolved;
        for (const auto& mount : mounts)
        {
            for (const auto& entry : mount.Catalog.Entries)
                resolved[entry.Id] = {entry, mount.Specification.Priority};
        }
        std::scoped_lock lock(m_Impl->Mutex);
        m_Impl->Mounts = std::move(mounts);
        m_Impl->Resolved = std::move(resolved);
        return true;
    }

    bool AssetSystem::PublishDevelopmentAsset(const AssetId id, Ref<Asset> asset)
    {
        m_Impl->RequireOwnerThread("PublishDevelopmentAsset");
        if (!id || !asset)
            throw std::invalid_argument("A development asset publication requires an ID and asset value.");

        Ref<Detail::AssetHandleState> state;
        bool reload = false;
        {
            std::scoped_lock lock(m_Impl->Mutex);
            if (!m_Impl->Open || m_Impl->Specification.Mode != AssetMode::Development)
                throw std::logic_error("Development asset publication requires an open development asset system.");
            const auto decoder = m_Impl->Decoders.find(asset->Type());
            if (decoder == m_Impl->Decoders.end())
                throw std::invalid_argument("No decoder is registered for the published development asset type.");
            if (const auto resolved = m_Impl->Resolved.find(id);
                resolved != m_Impl->Resolved.end() && resolved->second.Entry.Type != asset->Type())
                throw std::invalid_argument("The published development asset type does not match its catalog entry.");

            const auto existing = m_Impl->States.find(id);
            if (existing == m_Impl->States.end())
            {
                state = CreateRef<Detail::AssetHandleState>(id, asset->Type(), decoder->second.Fallback, ThreadHash());
                m_Impl->States.emplace(id, state);
            }
            else
            {
                state = existing->second;
                if (state->Type() != asset->Type())
                    throw std::invalid_argument("The published development asset type does not match its handle.");
                const auto current = state->State();
                if (current != AssetState::Ready && current != AssetState::Failed)
                    return false;
                reload = true;
            }
        }

        state->Commit(std::move(asset));
        {
            std::scoped_lock lock(m_Impl->Mutex);
            ++m_Impl->CompletedLoads;
            if (reload)
                ++m_Impl->Reloads;
        }
        if (m_Impl->Events)
            (void)m_Impl->Events->Dispatch(AssetLoadedEvent{state->Id(), state->Type(), state->Revision(), reload});
        return true;
    }

    bool AssetSystem::Reload(const AssetId id, const AssetPriority priority)
    {
        m_Impl->RequireOwnerThread("Reload");
        std::scoped_lock lock(m_Impl->Mutex);
        const auto state = m_Impl->States.find(id);
        const auto entry = m_Impl->Resolved.find(id);
        if (!m_Impl->Open || state == m_Impl->States.end() || entry == m_Impl->Resolved.end() ||
            m_Impl->QueueSize >= m_Impl->Specification.QueueCapacity)
            return false;
        const auto currentState = state->second->State();
        if (currentState == AssetState::Queued)
        {
            const auto pending = std::ranges::find_if(m_Impl->Completions,
                                                      [&state](const Impl::Completion& completion)
                                                      {
                                                          return completion.State.Get() == state->second.Get() &&
                                                                 !completion.CountedInQueue &&
                                                                 completion.Diagnostic.Operation == "resolve";
                                                      });
            if (pending == m_Impl->Completions.end())
                return false;
            const auto decoder = m_Impl->Decoders.find(state->second->Type());
            if (decoder == m_Impl->Decoders.end() || entry->second.Entry.Type != state->second->Type())
                return false;
            m_Impl->Completions.erase(pending);
            m_Impl->Jobs[static_cast<std::size_t>(priority)].push_back(
                {state->second, entry->second, decoder->second, false, ++m_Impl->Sequence});
            ++m_Impl->QueueSize;
            m_Impl->QueueHighWaterMark = std::max(m_Impl->QueueHighWaterMark, m_Impl->QueueSize);
            ++m_Impl->Reloads;
            m_Impl->WorkAvailable.notify_one();
            return true;
        }
        if (currentState != AssetState::Ready && currentState != AssetState::Failed)
            return false;
        const auto decoder = m_Impl->Decoders.find(state->second->Type());
        if (decoder == m_Impl->Decoders.end() || entry->second.Entry.Type != state->second->Type())
            return false;
        m_Impl->Jobs[static_cast<std::size_t>(priority)].push_back(
            {state->second, entry->second, decoder->second, true, ++m_Impl->Sequence});
        state->second->SetLoading(true);
        ++m_Impl->QueueSize;
        m_Impl->QueueHighWaterMark = std::max(m_Impl->QueueHighWaterMark, m_Impl->QueueSize);
        ++m_Impl->Reloads;
        m_Impl->WorkAvailable.notify_one();
        return true;
    }

    Ref<AssetStreamOperation> AssetSystem::ReadRangeAsync(const AssetId id, const std::uint64_t offset,
                                                          const std::size_t bytes)
    {
        m_Impl->RequireOwnerThread("ReadRangeAsync");
        std::scoped_lock lock(m_Impl->Mutex);
        if (!m_Impl->Open || m_Impl->Specification.Mode == AssetMode::Disabled)
            throw std::logic_error("Asset range reads are unavailable because the asset system is disabled or closed.");
        if (!id || bytes > m_Impl->Specification.MaximumStreamReadBytes ||
            offset > std::numeric_limits<std::uint64_t>::max() - bytes)
            throw std::invalid_argument("Asset stream range is invalid or exceeds the configured bound.");
        const auto entry = m_Impl->Resolved.find(id);
        if (entry == m_Impl->Resolved.end())
            throw std::invalid_argument("Asset stream range references an unavailable asset.");
        if (offset > entry->second.Entry.UncompressedBytes || bytes > entry->second.Entry.UncompressedBytes - offset)
            throw std::out_of_range("Asset stream range extends beyond the asset.");
        if (m_Impl->QueueSize >= m_Impl->Specification.QueueCapacity)
            throw std::runtime_error("Asset loading queue capacity was exhausted.");

        std::erase_if(m_Impl->StreamOperations,
                      [](const Ref<AssetStreamOperation>& operation)
                      {
                          const auto state = operation->State();
                          return state == AssetStreamState::Succeeded || state == AssetStreamState::Failed ||
                                 state == AssetStreamState::Cancelled;
                      });
        auto operation = CreateRef<AssetStreamOperation>();
        m_Impl->StreamJobs.push_back({operation, entry->second, offset, bytes});
        m_Impl->StreamOperations.push_back(operation);
        ++m_Impl->QueueSize;
        m_Impl->QueueHighWaterMark = std::max(m_Impl->QueueHighWaterMark, m_Impl->QueueSize);
        m_Impl->WorkAvailable.notify_one();
        return operation;
    }

    std::size_t AssetSystem::PumpCompletions()
    {
        m_Impl->RequireOwnerThread("PumpCompletions");
        std::deque<Impl::Completion> completions;
        {
            std::scoped_lock lock(m_Impl->Mutex);
            completions.swap(m_Impl->Completions);
            const auto counted =
                std::ranges::count_if(completions, [](const auto& completion) { return completion.CountedInQueue; });
            m_Impl->QueueSize = m_Impl->QueueSize >= static_cast<std::size_t>(counted)
                                    ? m_Impl->QueueSize - static_cast<std::size_t>(counted)
                                    : 0;
        }
        for (auto& completion : completions)
        {
            if (completion.Value)
            {
                completion.State->Commit(std::move(completion.Value));
                {
                    std::scoped_lock lock(m_Impl->Mutex);
                    ++m_Impl->CompletedLoads;
                }
                if (m_Impl->Events)
                    (void)m_Impl->Events->Dispatch(AssetLoadedEvent{completion.State->Id(), completion.State->Type(),
                                                                    completion.State->Revision(), completion.Reload});
            }
            else
            {
                completion.State->Fail(completion.Diagnostic, completion.Reload);
                try
                {
                    KEIRE_CORE_ERROR("Asset load failed for id={} type={} during {}{}: {}",
                                     completion.State->Id().ToString(), completion.State->Type().ToString(),
                                     completion.Diagnostic.Operation, completion.Reload ? " reload" : "",
                                     completion.Diagnostic.Message);
                }
                catch (...)
                {
                    std::fprintf(stderr, "Asset load failed: %s\n", completion.Diagnostic.Message.c_str());
                }
                {
                    std::scoped_lock lock(m_Impl->Mutex);
                    ++m_Impl->FailedLoads;
                }
                if (m_Impl->Events)
                    (void)m_Impl->Events->Dispatch(AssetLoadFailedEvent{
                        completion.State->Id(), completion.State->Type(), completion.Diagnostic, completion.Reload});
            }
        }
        (void)EvictUnused();
        return completions.size();
    }

    std::size_t AssetSystem::EvictUnused()
    {
        m_Impl->RequireOwnerThread("EvictUnused");
        std::scoped_lock lock(m_Impl->Mutex);
        std::size_t resident = 0;
        for (const auto& [id, state] : m_Impl->States)
        {
            if (const auto value = state->Current(); value && !state->UsingFallback())
                resident += value->ResidentBytes();
        }
        std::size_t evicted = 0;
        if (resident > m_Impl->Specification.ResidentCacheBudgetBytes)
        {
            for (auto iterator = m_Impl->States.begin();
                 iterator != m_Impl->States.end() && resident > m_Impl->Specification.ResidentCacheBudgetBytes;)
            {
                auto& state = iterator->second;
                if (state.UseCount() == 1 && state->State() == AssetState::Ready)
                {
                    const auto value = state->Current();
                    resident -= value ? value->ResidentBytes() : 0;
                    iterator = m_Impl->States.erase(iterator);
                    ++evicted;
                }
                else
                    ++iterator;
            }
        }
        m_Impl->Evictions += evicted;
        return evicted;
    }

    AssetSystemStatistics AssetSystem::Statistics() const
    {
        std::scoped_lock lock(m_Impl->Mutex);
        AssetSystemStatistics result;
        result.KnownAssets = m_Impl->States.size();
        result.QueueHighWaterMark = m_Impl->QueueHighWaterMark;
        result.CompletedLoads = m_Impl->CompletedLoads;
        result.FailedLoads = m_Impl->FailedLoads;
        result.Reloads = m_Impl->Reloads;
        result.Evictions = m_Impl->Evictions;
        for (const auto& [id, state] : m_Impl->States)
        {
            if (state->State() == AssetState::Queued)
                ++result.QueuedAssets;
            else if (state->State() == AssetState::Loading || state->State() == AssetState::Reloading)
                ++result.LoadingAssets;
            if (const auto value = state->Current(); value && !state->UsingFallback())
                result.ResidentBytes += value->ResidentBytes();
        }
        return result;
    }

    std::optional<AssetDerivedMetadata> AssetSystem::TryGetMetadata(const AssetId id) const
    {
        std::scoped_lock lock(m_Impl->Mutex);
        const auto found = m_Impl->Resolved.find(id);
        if (found == m_Impl->Resolved.end())
            return std::nullopt;
        return found->second.Entry.Metadata;
    }

    bool AssetSystem::IsOpen() const noexcept
    {
        std::scoped_lock lock(m_Impl->Mutex);
        return m_Impl->Open;
    }

    void AssetSystem::Close() noexcept
    {
        if (!m_Impl)
            return;
        std::vector<Ref<Detail::AssetHandleState>> cancelled;
        std::vector<Ref<AssetStreamOperation>> streamOperations;
        {
            std::scoped_lock lock(m_Impl->Mutex);
            if (!m_Impl->Open && m_Impl->Stopping)
                return;
            m_Impl->Open = false;
            m_Impl->Stopping = true;
            for (auto& queue : m_Impl->Jobs)
            {
                for (auto& job : queue)
                    cancelled.push_back(job.State);
                queue.clear();
            }
            for (auto& completion : m_Impl->Completions)
                cancelled.push_back(completion.State);
            m_Impl->Completions.clear();
            m_Impl->StreamJobs.clear();
            streamOperations = m_Impl->StreamOperations;
        }
        for (const auto& state : cancelled)
            state->Cancel();
        for (const auto& operation : streamOperations)
            operation->Cancel();
        m_Impl->WorkAvailable.notify_all();
        for (auto& worker : m_Impl->Workers)
        {
            if (worker.joinable())
                worker.join();
        }
        m_Impl->Workers.clear();
        for (const auto& [id, state] : m_Impl->States)
            state->Cancel();
    }
} // namespace Keire
