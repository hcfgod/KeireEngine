#include "KeireInternal/Assets/AssimpProjectIO.h"

#include "KeireInternal/Assets/AssetInternal.h"
#include "KeireInternal/FileSystem.h"

#include <assimp/IOStream.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

namespace Keire::Detail
{
    namespace
    {
        constexpr std::size_t MaximumReferenceLength = 4096;
        constexpr std::size_t MaximumSidecarFiles = 256;
        constexpr std::size_t MaximumAggregateBytes = std::size_t{1024} * 1024U * 1024U;

        [[nodiscard]] bool IsConfinedRelative(const std::filesystem::path& path, const bool allowCurrentDirectory)
        {
            const auto normalized = path.lexically_normal();
            if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory() ||
                normalized.empty() || (!allowCurrentDirectory && normalized == "."))
            {
                return false;
            }
            return normalized != ".." && *normalized.begin() != "..";
        }

        [[nodiscard]] int HexDigit(const char value) noexcept
        {
            if (value >= '0' && value <= '9')
                return value - '0';
            if (value >= 'a' && value <= 'f')
                return value - 'a' + 10;
            if (value >= 'A' && value <= 'F')
                return value - 'A' + 10;
            return -1;
        }

        [[nodiscard]] std::optional<std::string> DecodeReference(const std::string_view value)
        {
            if (value.empty() || value.size() > MaximumReferenceLength)
                return std::nullopt;

            std::string result;
            result.reserve(value.size());
            for (std::size_t index = 0; index < value.size(); ++index)
            {
                char character = value[index];
                if (character == '%')
                {
                    if (index + 2U >= value.size())
                        return std::nullopt;
                    const auto high = HexDigit(value[index + 1U]);
                    const auto low = HexDigit(value[index + 2U]);
                    if (high < 0 || low < 0)
                        return std::nullopt;
                    character = static_cast<char>((high << 4) | low);
                    index += 2U;
                }
                if (character == '\0' || character == '?' || character == '#')
                    return std::nullopt;
                result.push_back(character == '\\' ? '/' : character);
            }

            if (result.starts_with('/') || result.find(':') != std::string::npos)
            {
                return std::nullopt;
            }
            return result;
        }

        class AssimpProjectStream final : public Assimp::IOStream
        {
          public:
            explicit AssimpProjectStream(std::shared_ptr<const std::vector<std::byte>> bytes)
                : m_Bytes(std::move(bytes))
            {
            }

            [[nodiscard]] std::size_t Read(void* output, const std::size_t size, const std::size_t count) override
            {
                if (!output || size == 0 || count == 0 || m_Position >= m_Bytes->size())
                    return 0;
                const auto availableCount = (m_Bytes->size() - m_Position) / size;
                const auto readCount = std::min(count, availableCount);
                const auto readBytes = readCount * size;
                std::memcpy(output, m_Bytes->data() + m_Position, readBytes);
                m_Position += readBytes;
                return readCount;
            }

            [[nodiscard]] std::size_t Write(const void*, std::size_t, std::size_t) override { return 0; }

            [[nodiscard]] aiReturn Seek(const std::size_t offset, const aiOrigin origin) override
            {
                switch (origin)
                {
                case aiOrigin_SET:
                    if (offset > m_Bytes->size())
                        return aiReturn_FAILURE;
                    m_Position = offset;
                    return aiReturn_SUCCESS;
                case aiOrigin_CUR:
                    if (offset > m_Bytes->size() - m_Position)
                        return aiReturn_FAILURE;
                    m_Position += offset;
                    return aiReturn_SUCCESS;
                case aiOrigin_END:
                    if (offset > m_Bytes->size())
                        return aiReturn_FAILURE;
                    m_Position = m_Bytes->size() - offset;
                    return aiReturn_SUCCESS;
                default:
                    return aiReturn_FAILURE;
                }
            }

            [[nodiscard]] std::size_t Tell() const override { return m_Position; }
            [[nodiscard]] std::size_t FileSize() const override { return m_Bytes->size(); }
            void Flush() override {}

          private:
            std::shared_ptr<const std::vector<std::byte>> m_Bytes;
            std::size_t m_Position = 0;
        };
    } // namespace

    AssimpProjectIO::AssimpProjectIO(const AssetImportContext& context)
        : m_MaximumDependencyBytes(context.MaximumDependencyBytes), m_ReadProjectFile(context.ReadProjectFile)
    {
        if (context.ProjectRoot.empty() || context.SourceRoot.empty() || !m_ReadProjectFile ||
            m_MaximumDependencyBytes == 0)
        {
            throw std::invalid_argument("Assimp project IO requires complete project roots and a non-zero read limit.");
        }

        const auto projectRoot = context.ProjectRoot.lexically_normal();
        const auto sourceRoot = context.SourceRoot.lexically_normal();
        m_SourcePrefix = sourceRoot.lexically_relative(projectRoot).lexically_normal();
        const auto source = context.RelativePath.lexically_normal();
        if (!IsConfinedRelative(m_SourcePrefix, true) || !IsConfinedRelative(source, false) ||
            source.filename().empty())
        {
            throw std::invalid_argument("Assimp model sources must remain inside the project source root.");
        }
        m_SourceDirectory = source.parent_path();
    }

    bool AssimpProjectIO::Exists(const char* const file) const
    {
        const auto* cached = file ? Read(file) : nullptr;
        return cached && cached->Bytes;
    }

    char AssimpProjectIO::getOsSeparator() const { return '/'; }

    Assimp::IOStream* AssimpProjectIO::Open(const char* const file, const char* const mode)
    {
        if (!file || !mode)
            return nullptr;
        const std::string_view requestedMode(mode);
        if (requestedMode != "r" && requestedMode != "rb" && requestedMode != "rt")
        {
            Reject("Assimp model sidecars are read-only.");
            return nullptr;
        }
        const auto* cached = Read(file);
        return cached && cached->Bytes ? new AssimpProjectStream(cached->Bytes) : nullptr;
    }

    void AssimpProjectIO::Close(Assimp::IOStream* const file) { delete file; }

    std::optional<AssimpProjectFile> AssimpProjectIO::ReadReferencedFile(const std::string_view file)
    {
        const auto* cached = Read(file);
        if (!cached || !cached->Bytes)
            return std::nullopt;
        return AssimpProjectFile{cached->RelativePath, *cached->Bytes};
    }

    bool AssimpProjectIO::ValidateReference(const std::string_view file) const { return Resolve(file).has_value(); }

    const std::vector<AssetSourceDependency>& AssimpProjectIO::SourceDependencies() const noexcept
    {
        return m_SourceDependencies;
    }

    std::string_view AssimpProjectIO::LastReadFailure() const noexcept { return m_LastReadFailure; }

    std::string_view AssimpProjectIO::Violation() const noexcept { return m_Violation; }

    const AssimpProjectIO::CachedFile* AssimpProjectIO::Read(const std::string_view file) const
    {
        m_LastReadFailure.clear();
        const auto relative = Resolve(file);
        if (!relative)
            return nullptr;
        const auto key = relative->generic_string();
        if (const auto found = m_Cache.find(key); found != m_Cache.end())
        {
            m_LastReadFailure = found->second.Failure;
            return &found->second;
        }
        if (m_Cache.size() >= MaximumSidecarFiles)
        {
            Reject("Assimp model sidecars exceed the file-count limit.");
            return nullptr;
        }

        CachedFile cached;
        cached.RelativePath = *relative;
        std::vector<std::byte> bytes;
        try
        {
            bytes = m_ReadProjectFile(*relative);
        }
        catch (const std::exception& exception)
        {
            cached.Failure = "Could not read model sidecar '" + key + "': " + exception.what();
        }
        catch (...)
        {
            cached.Failure = "Could not read model sidecar '" + key + "': unknown project-file read failure.";
        }
        if (cached.Failure.empty())
        {
            if (bytes.size() > m_MaximumDependencyBytes)
            {
                Reject("Assimp model sidecar exceeds the per-file byte limit: " + key);
                cached.Failure = m_Violation;
            }
            else if (bytes.size() > MaximumAggregateBytes - m_TotalBytes)
            {
                Reject("Assimp model sidecars exceed the aggregate byte limit.");
                cached.Failure = m_Violation;
            }
            else
            {
                m_TotalBytes += bytes.size();
                cached.Bytes = std::make_shared<const std::vector<std::byte>>(std::move(bytes));
                m_SourceDependencies.push_back({*relative, DigestToString(Sha256(*cached.Bytes))});
            }
        }
        m_LastReadFailure = cached.Failure;
        return &m_Cache.emplace(key, std::move(cached)).first->second;
    }

    std::optional<std::filesystem::path> AssimpProjectIO::Resolve(const std::string_view file) const
    {
        const auto decoded = DecodeReference(file);
        if (!decoded)
        {
            Reject("Assimp model sidecar path is not a valid confined relative URI.");
            return std::nullopt;
        }

        try
        {
            const auto requested = PathFromUtf8(*decoded);
            if (!IsConfinedRelative(requested, false))
            {
                const auto normalized = requested.lexically_normal();
                if (!normalized.empty() && *normalized.begin() == "..")
                    Reject("Assimp model sidecar escapes the project source root: " + *decoded);
                else
                    Reject("Assimp model sidecar path is not a valid confined relative URI: " + *decoded);
                return std::nullopt;
            }
            const auto sourceRelative = (m_SourceDirectory / requested).lexically_normal();
            if (!IsConfinedRelative(sourceRelative, false))
            {
                Reject("Assimp model sidecar escapes the project source root: " + *decoded);
                return std::nullopt;
            }
            const auto projectRelative = (m_SourcePrefix / sourceRelative).lexically_normal();
            if (!IsConfinedRelative(projectRelative, false))
            {
                Reject("Assimp model sidecar escapes the project root: " + *decoded);
                return std::nullopt;
            }
            return projectRelative;
        }
        catch (const std::exception& exception)
        {
            Reject("Assimp model sidecar path could not be normalized: " + std::string(exception.what()));
            return std::nullopt;
        }
    }

    void AssimpProjectIO::Reject(std::string message) const
    {
        if (m_Violation.empty())
            m_Violation = std::move(message);
        m_LastReadFailure = m_Violation;
    }
} // namespace Keire::Detail
