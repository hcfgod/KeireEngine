#include "Keire/Replay/ReplaySystem.h"

#include "Keire/Diagnostics/Diagnostic.h"
#include "Keire/Memory/MemorySystem.h"
#include "KeireInternal/Assets/AssetInternal.h"
#include "KeireInternal/FileSystem.h"

#include <nlohmann/json.hpp>
#include <zstd.h>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstring>
#include <exception>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <stdexcept>
#include <thread>
#include <utility>

namespace Keire
{
    namespace
    {
        using Json = nlohmann::json;
        constexpr std::array<char, 12> ReplayMagic{'K', 'E', 'I', 'R', 'E', 'R', 'P', 'L', 'A', 'Y', '\r', '\n'};
        constexpr std::uint32_t ReplayVersion = 1;
        constexpr std::uint32_t ChunkMetadata = 1;
        constexpr std::uint32_t ChunkInput = 2;
        constexpr std::uint32_t ChunkCheckpoint = 3;
        constexpr std::uint32_t ChunkEnd = 4;
        constexpr std::uint32_t ChunkCompressed = 1;
        constexpr std::size_t ReplayFileHeaderBytes = ReplayMagic.size() + sizeof(ReplayVersion);
        constexpr std::size_t ReplayChunkHeaderBytes =
            sizeof(std::uint32_t) * 2U + sizeof(std::uint64_t) * 3U + Detail::Sha256Digest{}.size();
        constexpr std::size_t EncodedInputActionBytes = sizeof(std::uint64_t) * 7U + sizeof(std::uint32_t) * 3U + 3U;

        template <typename T> void AppendUnsigned(std::vector<std::byte>& output, const T value)
        {
            static_assert(std::is_unsigned_v<T>);
            for (std::size_t index = 0; index < sizeof(T); ++index)
                output.push_back(static_cast<std::byte>(value >> (index * 8U)));
        }

        template <typename T> [[nodiscard]] T ReadUnsigned(const std::span<const std::byte> input, std::size_t& offset)
        {
            static_assert(std::is_unsigned_v<T>);
            if (offset > input.size() || sizeof(T) > input.size() - offset)
                throw std::runtime_error("Replay chunk is truncated.");
            T result = 0;
            for (std::size_t index = 0; index < sizeof(T); ++index)
                result |= static_cast<T>(std::to_integer<std::uint8_t>(input[offset++])) << (index * 8U);
            return result;
        }

        void AppendBytes(std::vector<std::byte>& output, const std::span<const std::byte> bytes)
        {
            output.insert(output.end(), bytes.begin(), bytes.end());
        }

        void AppendString(std::vector<std::byte>& output, const std::string_view value)
        {
            if (value.size() > std::numeric_limits<std::uint32_t>::max())
                throw std::length_error("Replay string is too large.");
            AppendUnsigned(output, static_cast<std::uint32_t>(value.size()));
            AppendBytes(output, std::as_bytes(std::span(value)));
        }

        [[nodiscard]] std::string ReadString(const std::span<const std::byte> input, std::size_t& offset)
        {
            const auto size = ReadUnsigned<std::uint32_t>(input, offset);
            if (offset > input.size() || size > input.size() - offset)
                throw std::runtime_error("Replay string is truncated.");
            std::string result(reinterpret_cast<const char*>(input.data() + offset), size);
            offset += size;
            return result;
        }

        void RequireAppendCapacity(const std::vector<std::byte>& output, const std::size_t additional,
                                   const std::size_t maximumBytes)
        {
            if (output.size() > maximumBytes || additional > maximumBytes - output.size())
                throw std::runtime_error("Replay output exceeds the configured file-size limit.");
        }

        [[nodiscard]] std::vector<std::byte> Compress(const std::span<const std::byte> input,
                                                      const std::size_t maximumBytes)
        {
            const auto bound = ZSTD_compressBound(input.size());
            if (ZSTD_isError(bound) || bound > maximumBytes)
                throw std::runtime_error("Replay checkpoint exceeds the configured compressed-size limit.");
            std::vector<std::byte> result(bound);
            const auto bytes = ZSTD_compress(result.data(), result.size(), input.data(), input.size(), 3);
            if (ZSTD_isError(bytes))
                throw std::runtime_error(std::string("Replay checkpoint compression failed: ") +
                                         ZSTD_getErrorName(bytes));
            result.resize(bytes);
            return result;
        }

        [[nodiscard]] std::vector<std::byte> Decompress(const std::span<const std::byte> input,
                                                        const std::size_t outputBytes)
        {
            std::vector<std::byte> result(outputBytes);
            const auto bytes = ZSTD_decompress(result.data(), result.size(), input.data(), input.size());
            if (ZSTD_isError(bytes) || bytes != outputBytes)
                throw std::runtime_error("Replay checkpoint decompression failed.");
            return result;
        }

        [[nodiscard]] std::vector<std::byte> EncodeInput(const FixedTickInputSnapshot& snapshot,
                                                         const Detail::Sha256Digest& state,
                                                         const std::size_t maximumBytes)
        {
            constexpr std::size_t fixedBytes = sizeof(snapshot.Tick) + sizeof(snapshot.InputMapFingerprint) +
                                               sizeof(std::uint32_t) + Detail::Sha256Digest{}.size();
            if (snapshot.Actions.size() > std::numeric_limits<std::uint32_t>::max() || maximumBytes < fixedBytes ||
                snapshot.Actions.size() > (maximumBytes - fixedBytes) / EncodedInputActionBytes)
            {
                throw std::length_error("Replay input exceeds the configured file-size limit.");
            }
            std::vector<std::byte> result;
            result.reserve(fixedBytes + snapshot.Actions.size() * EncodedInputActionBytes);
            AppendUnsigned(result, snapshot.Tick);
            AppendUnsigned(result, snapshot.InputMapFingerprint);
            AppendUnsigned(result, static_cast<std::uint32_t>(snapshot.Actions.size()));
            for (const auto& action : snapshot.Actions)
            {
                AppendUnsigned(result, action.Context);
                AppendUnsigned(result, action.ContextAsset.High());
                AppendUnsigned(result, action.ContextAsset.Low());
                AppendUnsigned(result, action.Map.High());
                AppendUnsigned(result, action.Map.Low());
                AppendUnsigned(result, action.Action.High());
                AppendUnsigned(result, action.Action.Low());
                AppendUnsigned(result, action.User.Value());
                AppendUnsigned(result, static_cast<std::uint8_t>(action.Phase));
                AppendUnsigned(result, static_cast<std::uint8_t>(action.Value.Type));
                AppendUnsigned(result, std::bit_cast<std::uint32_t>(action.Value.X));
                AppendUnsigned(result, std::bit_cast<std::uint32_t>(action.Value.Y));
                const auto flags = static_cast<std::uint8_t>((action.Started ? 1U : 0U) | (action.Performed ? 2U : 0U) |
                                                             (action.Canceled ? 4U : 0U));
                AppendUnsigned(result, flags);
            }
            AppendBytes(result, state);
            return result;
        }

        struct DecodedInput final
        {
            FixedTickInputSnapshot Snapshot;
            Detail::Sha256Digest State{};
        };

        [[nodiscard]] DecodedInput DecodeInput(const std::span<const std::byte> bytes)
        {
            DecodedInput result;
            std::size_t offset = 0;
            result.Snapshot.Tick = ReadUnsigned<std::uint64_t>(bytes, offset);
            result.Snapshot.InputMapFingerprint = ReadUnsigned<std::uint64_t>(bytes, offset);
            const auto count = ReadUnsigned<std::uint32_t>(bytes, offset);
            if (offset > bytes.size() || result.State.size() > bytes.size() - offset ||
                count > (bytes.size() - offset - result.State.size()) / EncodedInputActionBytes)
            {
                throw std::runtime_error("Replay input action count exceeds the chunk payload.");
            }
            result.Snapshot.Actions.reserve(count);
            for (std::uint32_t index = 0; index < count; ++index)
            {
                FixedTickInputAction action;
                action.Context = ReadUnsigned<std::uint64_t>(bytes, offset);
                action.ContextAsset = {ReadUnsigned<std::uint64_t>(bytes, offset),
                                       ReadUnsigned<std::uint64_t>(bytes, offset)};
                action.Map = {ReadUnsigned<std::uint64_t>(bytes, offset), ReadUnsigned<std::uint64_t>(bytes, offset)};
                action.Action = {ReadUnsigned<std::uint64_t>(bytes, offset),
                                 ReadUnsigned<std::uint64_t>(bytes, offset)};
                action.User = InputUserId(ReadUnsigned<std::uint32_t>(bytes, offset));
                const auto phase = ReadUnsigned<std::uint8_t>(bytes, offset);
                const auto valueType = ReadUnsigned<std::uint8_t>(bytes, offset);
                if (phase > static_cast<std::uint8_t>(InputActionPhase::Canceled) ||
                    valueType > static_cast<std::uint8_t>(InputValueType::Axis2D))
                {
                    throw std::runtime_error("Replay input action contains an unsupported enum value.");
                }
                action.Phase = static_cast<InputActionPhase>(phase);
                action.Value.Type = static_cast<InputValueType>(valueType);
                action.Value.X = std::bit_cast<float>(ReadUnsigned<std::uint32_t>(bytes, offset));
                action.Value.Y = std::bit_cast<float>(ReadUnsigned<std::uint32_t>(bytes, offset));
                const auto flags = ReadUnsigned<std::uint8_t>(bytes, offset);
                if ((flags & ~std::uint8_t{7}) != 0)
                    throw std::runtime_error("Replay input action contains unsupported flags.");
                action.Started = (flags & 1U) != 0;
                action.Performed = (flags & 2U) != 0;
                action.Canceled = (flags & 4U) != 0;
                result.Snapshot.Actions.push_back(action);
            }
            if (offset > bytes.size() || result.State.size() != bytes.size() - offset)
                throw std::runtime_error("Replay input chunk has an invalid state digest.");
            std::ranges::copy(bytes.subspan(offset), result.State.begin());
            return result;
        }

        void AddChunk(std::vector<std::byte>& output, const std::uint32_t type, const std::uint32_t flags,
                      const std::uint64_t tick, const std::span<const std::byte> stored,
                      const std::span<const std::byte> uncompressed, const std::size_t maximumBytes)
        {
            RequireAppendCapacity(output, ReplayChunkHeaderBytes, maximumBytes);
            if (stored.size() > maximumBytes - output.size() - ReplayChunkHeaderBytes)
                throw std::runtime_error("Replay output exceeds the configured file-size limit.");
            AppendUnsigned(output, type);
            AppendUnsigned(output, flags);
            AppendUnsigned(output, tick);
            AppendUnsigned(output, static_cast<std::uint64_t>(uncompressed.size()));
            AppendUnsigned(output, static_cast<std::uint64_t>(stored.size()));
            AppendBytes(output, Detail::Sha256(uncompressed));
            AppendBytes(output, stored);
        }

        [[nodiscard]] bool FingerprintsMatch(const ReplayFingerprints& expected, const ReplayFingerprints& actual)
        {
            const auto matches = [](const std::string& wanted, const std::string& found)
            { return wanted.empty() || wanted == found; };
            return matches(expected.EngineBuild, actual.EngineBuild) && matches(expected.Project, actual.Project) &&
                   matches(expected.Modules, actual.Modules) && matches(expected.Content, actual.Content) &&
                   matches(expected.DeterministicConfiguration, actual.DeterministicConfiguration);
        }
    } // namespace

    class ReplaySystem::Impl final
    {
      public:
        struct TickRecord final
        {
            FixedTickInputSnapshot Input;
            Detail::Sha256Digest State{};
            std::vector<std::byte> Encoded;
        };

        struct Checkpoint final
        {
            std::uint64_t Tick = 0;
            std::vector<std::byte> Data;
            std::vector<std::byte> Stored;
        };

        Impl(ReplaySystemSpecification specification, Ref<DiagnosticSink> diagnostics, Ref<MemorySystem> memory)
            : Specification(specification), Diagnostics(std::move(diagnostics)), Memory(std::move(memory)),
              Owner(std::this_thread::get_id())
        {
            if (Specification.CheckpointIntervalTicks == 0 || Specification.RewindBudgetBytes == 0 ||
                Specification.MaximumReplayBytes < 1024 ||
                Specification.RewindBudgetBytes > Specification.MaximumReplayBytes)
                throw std::invalid_argument("Replay system limits are invalid.");
            if (Memory)
                MemoryDomain = Memory->RegisterDomain("Replay");
        }

        void RequireOwner(const char* operation) const
        {
            if (std::this_thread::get_id() != Owner)
                throw std::logic_error(std::string("ReplaySystem::") + operation + " must run on the owner thread.");
            if (!Open)
                throw std::logic_error("ReplaySystem is closed.");
        }

        void RequireCheckpointCapacity(const std::vector<std::byte>& output, const std::size_t additional) const
        {
            if (output.size() > Specification.RewindBudgetBytes ||
                additional > Specification.RewindBudgetBytes - output.size())
            {
                throw std::length_error("Replay checkpoints exceed the configured rewind-memory budget.");
            }
        }

        [[nodiscard]] std::vector<std::byte> CaptureCheckpoint() const
        {
            if (Serializers.size() > std::numeric_limits<std::uint32_t>::max())
                throw std::length_error("Replay checkpoint contains too many serializers.");
            std::vector<std::byte> result;
            RequireCheckpointCapacity(result, sizeof(std::uint32_t));
            AppendUnsigned(result, static_cast<std::uint32_t>(Serializers.size()));
            for (const auto& [id, serializer] : Serializers)
            {
                const auto state = serializer.Capture();
                RequireCheckpointCapacity(result, sizeof(std::uint32_t));
                const auto afterStringLength = result.size() + sizeof(std::uint32_t);
                if (id.size() > Specification.RewindBudgetBytes - afterStringLength)
                    throw std::length_error("Replay checkpoints exceed the configured rewind-memory budget.");
                AppendString(result, id);
                RequireCheckpointCapacity(result, sizeof(std::uint32_t) + sizeof(std::uint64_t));
                AppendUnsigned(result, serializer.Version);
                AppendUnsigned(result, static_cast<std::uint64_t>(state.size()));
                RequireCheckpointCapacity(result, state.size());
                AppendBytes(result, state);
            }
            return result;
        }

        void RestoreCheckpoint(const std::span<const std::byte> bytes) const
        {
            struct RestoreEntry final
            {
                const ReplaySerializerRegistration* Serializer = nullptr;
                std::span<const std::byte> State;
            };

            std::size_t offset = 0;
            const auto count = ReadUnsigned<std::uint32_t>(bytes, offset);
            constexpr std::size_t minimumSerializerBytes = sizeof(std::uint32_t) * 2U + sizeof(std::uint64_t);
            if (count > (bytes.size() - offset) / minimumSerializerBytes)
                throw std::runtime_error("Replay checkpoint serializer count exceeds the payload.");
            std::vector<RestoreEntry> entries;
            entries.reserve(count);
            std::map<std::string, bool, std::less<>> seen;
            for (std::uint32_t index = 0; index < count; ++index)
            {
                const auto id = ReadString(bytes, offset);
                const auto version = ReadUnsigned<std::uint32_t>(bytes, offset);
                const auto size = ReadUnsigned<std::uint64_t>(bytes, offset);
                if (offset > bytes.size() || size > bytes.size() - offset)
                    throw std::runtime_error("Replay checkpoint serializer payload is truncated.");
                const auto serializer = Serializers.find(id);
                if (serializer == Serializers.end() || serializer->second.Version != version)
                    throw std::runtime_error("Replay checkpoint serializer is missing or version-mismatched: " + id);
                if (!seen.emplace(id, true).second)
                    throw std::runtime_error("Replay checkpoint contains a duplicate serializer: " + id);
                entries.push_back(
                    {std::addressof(serializer->second), bytes.subspan(offset, static_cast<std::size_t>(size))});
                offset += static_cast<std::size_t>(size);
            }
            if (offset != bytes.size())
                throw std::runtime_error("Replay checkpoint contains trailing data.");
            if (entries.size() != Serializers.size())
                throw std::runtime_error("Replay checkpoint does not contain every registered serializer.");

            std::vector<std::vector<std::byte>> rollback;
            rollback.reserve(entries.size());
            for (const auto& entry : entries)
                rollback.push_back(entry.Serializer->Capture());
            try
            {
                for (const auto& entry : entries)
                    entry.Serializer->Restore(entry.State);
            }
            catch (...)
            {
                const auto failure = std::current_exception();
                for (std::size_t index = entries.size(); index > 0; --index)
                {
                    try
                    {
                        entries[index - 1U].Serializer->Restore(rollback[index - 1U]);
                    }
                    catch (...)
                    {
                    }
                }
                std::rethrow_exception(failure);
            }
        }

        [[nodiscard]] Detail::Sha256Digest StateHash() const { return Detail::Sha256(CaptureCheckpoint()); }

        void AddCheckpoint(const std::uint64_t tick)
        {
            auto checkpoint = Checkpoint{tick, CaptureCheckpoint()};
            if (RewindBytes > Specification.RewindBudgetBytes ||
                checkpoint.Data.size() > Specification.RewindBudgetBytes - RewindBytes)
            {
                throw std::length_error("Replay checkpoints exceed the configured rewind-memory budget.");
            }
            checkpoint.Stored = Compress(checkpoint.Data, Specification.MaximumReplayBytes);
            const auto encodedBytes = EncodedChunkBytes(checkpoint.Stored.size());
            RequireRecordingCapacity(encodedBytes);
            const auto checkpointBytes = checkpoint.Data.size();
            Checkpoints.push_back(std::move(checkpoint));
            RewindBytes += checkpointBytes;
            RecordingBytes += encodedBytes;
            UpdateMemory();
        }

        [[nodiscard]] std::string MetadataText() const
        {
            const Json metadata{{"profile", static_cast<std::uint8_t>(Profile)},
                                {"checkpointInterval", Specification.CheckpointIntervalTicks},
                                {"engineBuild", Fingerprints.EngineBuild},
                                {"project", Fingerprints.Project},
                                {"modules", Fingerprints.Modules},
                                {"content", Fingerprints.Content},
                                {"deterministicConfiguration", Fingerprints.DeterministicConfiguration}};
            return metadata.dump();
        }

        [[nodiscard]] static std::size_t EncodedChunkBytes(const std::size_t payloadBytes)
        {
            if (payloadBytes > (std::numeric_limits<std::size_t>::max)() - ReplayChunkHeaderBytes)
                throw std::length_error("Replay chunk size overflows the addressable range.");
            return ReplayChunkHeaderBytes + payloadBytes;
        }

        void RequireRecordingCapacity(const std::size_t additional) const
        {
            if (RecordingBytes > Specification.MaximumReplayBytes ||
                additional > Specification.MaximumReplayBytes - RecordingBytes)
            {
                throw std::length_error("Replay recording exceeds the configured file-size limit.");
            }
        }

        void SetFailure(std::string_view operation, std::string_view message) noexcept
        {
            Session.State = ReplaySessionState::Failed;
            try
            {
                Session.Diagnostic = std::string(operation) + ": " + std::string(message);
                if (Diagnostics)
                {
                    Diagnostics->Report(
                        {DiagnosticId("KEIRE-REPLAY-0002"), DiagnosticSeverity::Error, Session.Diagnostic});
                }
            }
            catch (...)
            {
                Session.Diagnostic.clear();
            }
        }

        void SetCurrentExceptionFailure(const std::string_view operation) noexcept
        {
            try
            {
                throw;
            }
            catch (const std::exception& error)
            {
                SetFailure(operation, error.what());
            }
            catch (...)
            {
                SetFailure(operation, "unknown failure");
            }
        }

        void UpdateMemory() noexcept
        {
            if (Memory && MemoryDomain)
            {
                std::size_t bytes = RewindBytes;
                for (const auto& tick : Ticks)
                    bytes += tick.Input.Actions.capacity() * sizeof(FixedTickInputAction) + tick.Encoded.capacity() +
                             sizeof(TickRecord);
                for (const auto& checkpoint : Checkpoints)
                    bytes += checkpoint.Data.capacity() - checkpoint.Data.size() + checkpoint.Stored.capacity();
                Memory->ReportExternal(MemoryDomain, bytes);
            }
        }

        void WriteRecording()
        {
            std::vector<std::byte> output;
            RequireAppendCapacity(output, ReplayFileHeaderBytes, Specification.MaximumReplayBytes);
            AppendBytes(output, std::as_bytes(std::span(ReplayMagic)));
            AppendUnsigned(output, ReplayVersion);
            const auto metadataText = MetadataText();
            AddChunk(output, ChunkMetadata, 0, 0, std::as_bytes(std::span(metadataText)),
                     std::as_bytes(std::span(metadataText)), Specification.MaximumReplayBytes);
            for (const auto& tick : Ticks)
            {
                AddChunk(output, ChunkInput, 0, tick.Input.Tick, tick.Encoded, tick.Encoded,
                         Specification.MaximumReplayBytes);
            }
            for (const auto& checkpoint : Checkpoints)
            {
                AddChunk(output, ChunkCheckpoint, ChunkCompressed, checkpoint.Tick, checkpoint.Stored, checkpoint.Data,
                         Specification.MaximumReplayBytes);
            }
            const auto fileDigest = Detail::Sha256(output);
            AddChunk(output, ChunkEnd, 0, Ticks.size(), fileDigest, fileDigest, Specification.MaximumReplayBytes);
            if (output.size() != RecordingBytes)
                throw std::logic_error("Replay recording size accounting diverged from its encoded output.");
            Detail::WriteFileAtomically(Path, output);
        }

        void ReadPlayback(const ReplayPlaybackRequest& request)
        {
            std::ifstream stream(request.Path, std::ios::binary | std::ios::ate);
            if (!stream)
                throw std::runtime_error("Replay file could not be opened.");
            const auto length = stream.tellg();
            if (length < 0 || static_cast<std::uint64_t>(length) > Specification.MaximumReplayBytes ||
                static_cast<std::uint64_t>(length) >
                    static_cast<std::uint64_t>((std::numeric_limits<std::streamsize>::max)()))
                throw std::runtime_error("Replay file exceeds the configured file-size limit.");
            std::vector<std::byte> bytes(static_cast<std::size_t>(length));
            stream.seekg(0, std::ios::beg);
            if (!bytes.empty() && !stream.read(reinterpret_cast<char*>(bytes.data()), length))
                throw std::runtime_error("Replay file is truncated.");
            if (bytes.size() < ReplayMagic.size() + sizeof(std::uint32_t) ||
                !std::ranges::equal(std::as_bytes(std::span(ReplayMagic)), std::span(bytes).first(ReplayMagic.size())))
                throw std::runtime_error("Replay file has an invalid signature.");
            std::size_t offset = ReplayMagic.size();
            if (ReadUnsigned<std::uint32_t>(bytes, offset) != ReplayVersion)
                throw std::runtime_error("Replay file version is unsupported.");
            bool metadataRead = false;
            bool checkpointRead = false;
            bool endRead = false;
            std::size_t decodedBytes = 0;
            while (offset < bytes.size())
            {
                const auto chunkStart = offset;
                const auto type = ReadUnsigned<std::uint32_t>(bytes, offset);
                const auto flags = ReadUnsigned<std::uint32_t>(bytes, offset);
                const auto tick = ReadUnsigned<std::uint64_t>(bytes, offset);
                const auto uncompressedSize = ReadUnsigned<std::uint64_t>(bytes, offset);
                const auto storedSize = ReadUnsigned<std::uint64_t>(bytes, offset);
                if (type < ChunkMetadata || type > ChunkEnd)
                    throw std::runtime_error("Replay file contains an unknown chunk type.");
                if ((!metadataRead && type != ChunkMetadata) || endRead)
                    throw std::runtime_error("Replay chunks are not in a valid structural order.");
                if (type == ChunkInput && checkpointRead)
                    throw std::runtime_error("Replay input chunks may not follow checkpoint chunks.");
                if (type == ChunkCheckpoint)
                    checkpointRead = true;
                if ((type == ChunkCheckpoint && flags != ChunkCompressed) || (type != ChunkCheckpoint && flags != 0))
                    throw std::runtime_error("Replay chunk contains unsupported flags.");
                if (uncompressedSize > Specification.MaximumReplayBytes ||
                    uncompressedSize > (std::numeric_limits<std::size_t>::max)() ||
                    uncompressedSize >
                        Specification.MaximumReplayBytes - std::min(decodedBytes, Specification.MaximumReplayBytes))
                {
                    throw std::runtime_error("Replay decoded data exceeds the configured size limit.");
                }
                Detail::Sha256Digest digest{};
                if (offset > bytes.size() || digest.size() > bytes.size() - offset)
                    throw std::runtime_error("Replay chunk digest is truncated.");
                std::ranges::copy(std::span(bytes).subspan(offset, digest.size()), digest.begin());
                offset += digest.size();
                if (storedSize > Specification.MaximumReplayBytes || offset > bytes.size() ||
                    storedSize > bytes.size() - offset)
                    throw std::runtime_error("Replay chunk payload is truncated or oversized.");
                const auto stored = std::span(bytes).subspan(offset, static_cast<std::size_t>(storedSize));
                offset += static_cast<std::size_t>(storedSize);
                if ((flags & ChunkCompressed) == 0 && storedSize != uncompressedSize)
                    throw std::runtime_error("Replay uncompressed chunk sizes do not match.");
                auto decoded = (flags & ChunkCompressed) != 0
                                   ? Decompress(stored, static_cast<std::size_t>(uncompressedSize))
                                   : std::vector<std::byte>(stored.begin(), stored.end());
                decodedBytes += decoded.size();
                if (decoded.size() != uncompressedSize || Detail::Sha256(decoded) != digest)
                    throw std::runtime_error("Replay chunk failed its SHA-256 integrity check.");
                if (type == ChunkMetadata)
                {
                    if (metadataRead || tick != 0)
                        throw std::runtime_error("Replay metadata chunk is duplicated or has an invalid tick.");
                    const auto metadata = Json::parse(reinterpret_cast<const char*>(decoded.data()),
                                                      reinterpret_cast<const char*>(decoded.data() + decoded.size()));
                    const auto profile = metadata.at("profile").get<std::uint8_t>();
                    if (profile > static_cast<std::uint8_t>(ReplayProfile::PerformanceCapture))
                        throw std::runtime_error("Replay metadata contains an unsupported profile.");
                    Profile = static_cast<ReplayProfile>(profile);
                    Fingerprints = {metadata.value("engineBuild", ""), metadata.value("project", ""),
                                    metadata.value("modules", ""), metadata.value("content", ""),
                                    metadata.value("deterministicConfiguration", "")};
                    metadataRead = true;
                }
                else if (type == ChunkInput)
                {
                    const auto input = DecodeInput(decoded);
                    if (input.Snapshot.Tick != tick || (!Ticks.empty() && tick <= Ticks.back().Input.Tick))
                        throw std::runtime_error("Replay input ticks are not strictly ordered.");
                    Ticks.push_back({input.Snapshot, input.State, {}});
                }
                else if (type == ChunkCheckpoint)
                {
                    if ((!Checkpoints.empty() && tick <= Checkpoints.back().Tick) ||
                        RewindBytes > Specification.RewindBudgetBytes ||
                        decoded.size() > Specification.RewindBudgetBytes - RewindBytes)
                    {
                        throw std::runtime_error(
                            "Replay checkpoints are unordered or exceed the configured rewind-memory budget.");
                    }
                    const auto checkpointBytes = decoded.size();
                    Checkpoints.push_back({tick, std::move(decoded), {}});
                    RewindBytes += checkpointBytes;
                }
                else if (type == ChunkEnd)
                {
                    if (endRead || tick != Ticks.size())
                        throw std::runtime_error("Replay footer is duplicated or has an invalid tick count.");
                    if (decoded.size() != Detail::Sha256Digest{}.size() ||
                        !std::ranges::equal(decoded, Detail::Sha256(std::span(bytes).first(chunkStart))))
                        throw std::runtime_error("Replay file footer integrity check failed.");
                    if (offset != bytes.size())
                        throw std::runtime_error("Replay file contains data after its footer.");
                    endRead = true;
                }
            }
            if (!metadataRead || !endRead || Ticks.empty() || Checkpoints.empty())
                throw std::runtime_error("Replay file is incomplete.");
            if (Checkpoints.front().Tick != 0 || Checkpoints.back().Tick > Ticks.back().Input.Tick)
                throw std::runtime_error("Replay checkpoint ticks are outside the recorded timeline.");
            for (auto checkpoint = std::next(Checkpoints.begin()); checkpoint != Checkpoints.end(); ++checkpoint)
            {
                const auto input = std::ranges::lower_bound(Ticks, checkpoint->Tick, {},
                                                            [](const TickRecord& value) { return value.Input.Tick; });
                if (input == Ticks.end() || input->Input.Tick != checkpoint->Tick)
                    throw std::runtime_error("Replay checkpoint does not correspond to a recorded logical tick.");
            }
            if (!FingerprintsMatch(request.ExpectedFingerprints, Fingerprints))
                throw std::runtime_error("Replay fingerprints do not match the active certified configuration.");
            UpdateMemory();
        }

        ReplaySystemSpecification Specification;
        Ref<DiagnosticSink> Diagnostics;
        Ref<MemorySystem> Memory;
        Keire::MemoryDomain MemoryDomain;
        std::thread::id Owner;
        std::map<std::string, ReplaySerializerRegistration, std::less<>> Serializers;
        std::vector<TickRecord> Ticks;
        std::vector<Checkpoint> Checkpoints;
        ReplayFingerprints Fingerprints;
        ReplaySessionStatus Session;
        std::filesystem::path Path;
        FixedTickInputSnapshot CurrentInput;
        ReplayProfile Profile = ReplayProfile::StrictVerified;
        ReplaySessionState StateBeforePause = ReplaySessionState::Idle;
        std::size_t PlaybackIndex = 0;
        std::size_t RewindBytes = 0;
        std::size_t RecordingBytes = 0;
        bool StepRequested = false;
        bool Seeking = false;
        std::uint64_t SeekTarget = 0;
        bool Open = true;
    };

    ReplaySystem::ReplaySystem(ReplaySystemSpecification specification, Ref<DiagnosticSink> diagnostics,
                               Ref<MemorySystem> memory)
        : m_Impl(std::make_unique<Impl>(specification, std::move(diagnostics), std::move(memory)))
    {
    }

    ReplaySystem::~ReplaySystem() { Close(); }

    void ReplaySystem::RegisterSerializer(ReplaySerializerRegistration serializer)
    {
        m_Impl->RequireOwner("RegisterSerializer");
        if (m_Impl->Session.State != ReplaySessionState::Idle || serializer.Id.empty() || serializer.Version == 0 ||
            !serializer.Capture || !serializer.Restore)
            throw std::invalid_argument("Replay serializer registration is invalid, duplicated, or frozen.");
        auto id = serializer.Id;
        if (!m_Impl->Serializers.emplace(std::move(id), std::move(serializer)).second)
            throw std::invalid_argument("Replay serializer registration is invalid, duplicated, or frozen.");
    }

    void ReplaySystem::BeginRecording(ReplayRecordRequest request)
    {
        m_Impl->RequireOwner("BeginRecording");
        if (m_Impl->Session.State != ReplaySessionState::Idle || request.Path.empty())
            throw std::logic_error("Replay recording cannot start in the current state.");
        if (request.Profile == ReplayProfile::StrictVerified &&
            std::ranges::any_of(m_Impl->Serializers, [](const auto& item) { return !item.second.Deterministic; }))
            throw std::logic_error("Strict replay recording requires deterministic serializers for every subsystem.");
        try
        {
            m_Impl->Ticks.clear();
            m_Impl->Checkpoints.clear();
            m_Impl->RewindBytes = 0;
            m_Impl->RecordingBytes = ReplayFileHeaderBytes;
            m_Impl->Path = std::move(request.Path);
            m_Impl->Profile = request.Profile;
            m_Impl->Fingerprints = std::move(request.Fingerprints);
            const auto metadataBytes = Impl::EncodedChunkBytes(m_Impl->MetadataText().size());
            m_Impl->RequireRecordingCapacity(metadataBytes);
            m_Impl->RecordingBytes += metadataBytes;
            const auto footerBytes = Impl::EncodedChunkBytes(Detail::Sha256Digest{}.size());
            m_Impl->RequireRecordingCapacity(footerBytes);
            m_Impl->RecordingBytes += footerBytes;
            m_Impl->Session = {.State = ReplaySessionState::Recording, .Profile = m_Impl->Profile};
            m_Impl->AddCheckpoint(0);
            m_Impl->Session.CheckpointCount = 1;
        }
        catch (...)
        {
            m_Impl->Ticks.clear();
            m_Impl->Checkpoints.clear();
            m_Impl->RewindBytes = 0;
            m_Impl->RecordingBytes = 0;
            m_Impl->SetCurrentExceptionFailure("Replay recording startup failed");
            m_Impl->UpdateMemory();
            throw;
        }
    }

    void ReplaySystem::BeginPlayback(ReplayPlaybackRequest request)
    {
        m_Impl->RequireOwner("BeginPlayback");
        if (m_Impl->Session.State != ReplaySessionState::Idle)
            throw std::logic_error("Replay playback cannot start in the current state.");
        try
        {
            m_Impl->Ticks.clear();
            m_Impl->Checkpoints.clear();
            m_Impl->RewindBytes = 0;
            m_Impl->RecordingBytes = 0;
            m_Impl->ReadPlayback(request);
            if (m_Impl->Profile == ReplayProfile::StrictVerified &&
                std::ranges::any_of(m_Impl->Serializers, [](const auto& item) { return !item.second.Deterministic; }))
            {
                throw std::logic_error(
                    "Strict replay playback requires deterministic serializers for every subsystem.");
            }
            m_Impl->PlaybackIndex = 0;
            m_Impl->Seeking = false;
            m_Impl->Session = {.State = request.Verify ? ReplaySessionState::Verifying : ReplaySessionState::Playing,
                               .Profile = m_Impl->Profile,
                               .TickCount = m_Impl->Ticks.size(),
                               .CheckpointCount = m_Impl->Checkpoints.size()};
            m_Impl->RestoreCheckpoint(m_Impl->Checkpoints.front().Data);
        }
        catch (...)
        {
            m_Impl->Ticks.clear();
            m_Impl->Checkpoints.clear();
            m_Impl->RewindBytes = 0;
            m_Impl->RecordingBytes = 0;
            m_Impl->SetCurrentExceptionFailure("Replay playback startup failed");
            m_Impl->UpdateMemory();
            throw;
        }
    }

    void ReplaySystem::Stop()
    {
        m_Impl->RequireOwner("Stop");
        if (m_Impl->Session.State == ReplaySessionState::Recording)
        {
            try
            {
                m_Impl->WriteRecording();
            }
            catch (...)
            {
                m_Impl->SetCurrentExceptionFailure("Replay recording finalization failed");
                throw;
            }
        }
        m_Impl->Session.State = ReplaySessionState::Idle;
        m_Impl->CurrentInput = {};
        m_Impl->StepRequested = false;
        m_Impl->Seeking = false;
    }

    void ReplaySystem::Pause(const bool paused)
    {
        m_Impl->RequireOwner("Pause");
        if (paused && (m_Impl->Session.State == ReplaySessionState::Playing ||
                       m_Impl->Session.State == ReplaySessionState::Verifying))
        {
            m_Impl->StateBeforePause = m_Impl->Session.State;
            m_Impl->Session.State = ReplaySessionState::Paused;
        }
        else if (!paused && m_Impl->Session.State == ReplaySessionState::Paused)
            m_Impl->Session.State = m_Impl->StateBeforePause;
    }

    void ReplaySystem::Step()
    {
        m_Impl->RequireOwner("Step");
        if (m_Impl->Session.State != ReplaySessionState::Paused)
            throw std::logic_error("Replay stepping requires paused playback.");
        m_Impl->StepRequested = true;
    }

    bool ReplaySystem::Seek(const std::uint64_t tick)
    {
        m_Impl->RequireOwner("Seek");
        if (!ReplacesGameplayInput())
            return false;
        const auto input = std::ranges::lower_bound(m_Impl->Ticks, tick, {},
                                                    [](const Impl::TickRecord& value) { return value.Input.Tick; });
        if (input == m_Impl->Ticks.end() || input->Input.Tick != tick)
            return false;
        const auto checkpoint = std::ranges::upper_bound(m_Impl->Checkpoints, tick, {},
                                                         [](const Impl::Checkpoint& value) { return value.Tick; });
        if (checkpoint == m_Impl->Checkpoints.begin())
            return false;
        const auto selected = std::prev(checkpoint);
        try
        {
            m_Impl->RestoreCheckpoint(selected->Data);
        }
        catch (...)
        {
            m_Impl->SetCurrentExceptionFailure("Replay seek restoration failed");
            throw;
        }
        const auto resume = std::ranges::upper_bound(m_Impl->Ticks, selected->Tick, {},
                                                     [](const Impl::TickRecord& value) { return value.Input.Tick; });
        m_Impl->PlaybackIndex = static_cast<std::size_t>(std::distance(m_Impl->Ticks.begin(), resume));
        m_Impl->Session.CurrentTick = selected->Tick;
        m_Impl->SeekTarget = tick;
        m_Impl->Seeking = selected->Tick != tick;
        if (!m_Impl->Seeking)
            m_Impl->PlaybackIndex = static_cast<std::size_t>(std::distance(m_Impl->Ticks.begin(), std::next(input)));
        return true;
    }

    FixedTickInputSnapshot ReplaySystem::BeginFixedTick(const FixedTickInputSnapshot& liveInput)
    {
        m_Impl->RequireOwner("BeginFixedTick");
        if (m_Impl->Session.State == ReplaySessionState::Recording)
        {
            m_Impl->CurrentInput = liveInput;
            return liveInput;
        }
        if (!ReplacesGameplayInput())
            return liveInput;
        if (m_Impl->Session.State == ReplaySessionState::Paused && !m_Impl->StepRequested && !m_Impl->Seeking)
            return {};
        if (m_Impl->PlaybackIndex >= m_Impl->Ticks.size())
        {
            m_Impl->Session.State = ReplaySessionState::Completed;
            return {};
        }
        m_Impl->CurrentInput = m_Impl->Ticks[m_Impl->PlaybackIndex].Input;
        if (liveInput.InputMapFingerprint != 0 &&
            liveInput.InputMapFingerprint != m_Impl->CurrentInput.InputMapFingerprint)
        {
            m_Impl->SetFailure("Replay input replacement failed",
                               "the active input-map fingerprint does not match the recording");
            throw std::runtime_error("Replay input-map fingerprint does not match the active input contexts.");
        }
        return m_Impl->CurrentInput;
    }

    void ReplaySystem::EndFixedTick(const std::uint64_t tick)
    {
        m_Impl->RequireOwner("EndFixedTick");
        if (m_Impl->Session.State == ReplaySessionState::Recording)
        {
            try
            {
                if (m_Impl->CurrentInput.Tick != tick)
                    throw std::logic_error("Replay recording received mismatched logical and application ticks.");
                const auto state = m_Impl->StateHash();
                auto encoded = EncodeInput(m_Impl->CurrentInput, state, m_Impl->Specification.MaximumReplayBytes);
                const auto encodedBytes = Impl::EncodedChunkBytes(encoded.size());
                m_Impl->RequireRecordingCapacity(encodedBytes);
                m_Impl->Ticks.push_back({m_Impl->CurrentInput, state, std::move(encoded)});
                m_Impl->RecordingBytes += encodedBytes;
                m_Impl->Session.CurrentTick = m_Impl->CurrentInput.Tick;
                m_Impl->Session.TickCount = m_Impl->Ticks.size();
                if (m_Impl->CurrentInput.Tick != 0 &&
                    m_Impl->CurrentInput.Tick % m_Impl->Specification.CheckpointIntervalTicks == 0)
                {
                    m_Impl->AddCheckpoint(m_Impl->CurrentInput.Tick);
                    m_Impl->Session.CheckpointCount = m_Impl->Checkpoints.size();
                }
                m_Impl->UpdateMemory();
            }
            catch (...)
            {
                m_Impl->SetCurrentExceptionFailure("Replay tick recording failed");
                throw;
            }
            return;
        }
        if (!ReplacesGameplayInput() || m_Impl->PlaybackIndex >= m_Impl->Ticks.size())
            return;
        if (m_Impl->Session.State == ReplaySessionState::Paused && !m_Impl->StepRequested && !m_Impl->Seeking)
            return;
        Detail::Sha256Digest actual;
        try
        {
            actual = m_Impl->StateHash();
        }
        catch (...)
        {
            m_Impl->SetCurrentExceptionFailure("Replay state verification failed");
            throw;
        }
        const auto& expected = m_Impl->Ticks[m_Impl->PlaybackIndex];
        const auto logicalTick = expected.Input.Tick;
        if ((m_Impl->Profile == ReplayProfile::StrictVerified ||
             m_Impl->Session.State == ReplaySessionState::Verifying ||
             m_Impl->StateBeforePause == ReplaySessionState::Verifying) &&
            actual != expected.State)
        {
            m_Impl->Session.State = ReplaySessionState::Diverged;
            m_Impl->Session.Divergence =
                ReplayDivergence{logicalTick, expected.State, actual, "Canonical replay state hash diverged."};
            if (m_Impl->Diagnostics)
                m_Impl->Diagnostics->Report(
                    {DiagnosticId("KEIRE-REPLAY-0001"), DiagnosticSeverity::Error,
                     "Replay verification diverged at tick " + std::to_string(logicalTick) + "."});
            return;
        }
        ++m_Impl->PlaybackIndex;
        m_Impl->Session.CurrentTick = logicalTick;
        if (m_Impl->Seeking && logicalTick >= m_Impl->SeekTarget)
            m_Impl->Seeking = false;
        if (m_Impl->StepRequested)
            m_Impl->StepRequested = false;
        if (m_Impl->PlaybackIndex >= m_Impl->Ticks.size())
            m_Impl->Session.State = ReplaySessionState::Completed;
    }

    ReplaySessionStatus ReplaySystem::Status() const { return m_Impl->Session; }

    bool ReplaySystem::ReplacesGameplayInput() const noexcept
    {
        return m_Impl->Session.State == ReplaySessionState::Playing ||
               m_Impl->Session.State == ReplaySessionState::Verifying ||
               m_Impl->Session.State == ReplaySessionState::Paused;
    }

    bool ReplaySystem::ShouldAdvanceFixedTick() const noexcept
    {
        return m_Impl->Session.State != ReplaySessionState::Paused || m_Impl->StepRequested || m_Impl->Seeking;
    }

    bool ReplaySystem::UsesStrictScheduling() const noexcept
    {
        return m_Impl->Profile == ReplayProfile::StrictVerified && ReplacesGameplayInput();
    }

    bool ReplaySystem::IsOpen() const noexcept { return m_Impl->Open; }

    void ReplaySystem::Close() noexcept
    {
        if (!m_Impl || !m_Impl->Open)
            return;
        try
        {
            if (m_Impl->Session.State == ReplaySessionState::Recording)
                m_Impl->WriteRecording();
        }
        catch (...)
        {
            m_Impl->SetCurrentExceptionFailure("Replay recording finalization failed");
        }
        m_Impl->Open = false;
        if (m_Impl->Session.State != ReplaySessionState::Failed)
            m_Impl->Session.State = ReplaySessionState::Idle;
        m_Impl->Ticks.clear();
        m_Impl->Checkpoints.clear();
        m_Impl->RewindBytes = 0;
        m_Impl->RecordingBytes = 0;
        m_Impl->UpdateMemory();
    }
} // namespace Keire
