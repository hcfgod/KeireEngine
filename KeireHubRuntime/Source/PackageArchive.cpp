#include "KeireHubRuntime/PackageArchive.h"

#include <KeireHubRuntimeInternal/DistributionEncoding.h>
#include <KeireHubRuntimeInternal/PackageArchiveOutput.h>
#include <KeireHubRuntimeInternal/Persistence.h>
#include <KeireHubRuntimeInternal/Sha256.h>

#include <zstd.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <ranges>
#include <set>
#include <stdexcept>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace KeireHub
{
    namespace
    {
        constexpr std::array<char, 8> ArchiveMagic{'K', 'E', 'I', 'R', 'P', 'K', 'G', '1'};
        constexpr std::uint32_t ArchiveSchemaVersion = 1;
        constexpr std::uint8_t EndRecord = 0;
        constexpr std::uint8_t FileRecord = 1;
        constexpr std::size_t BufferBytes = std::size_t{256U} * 1024U;
        constexpr std::size_t MaximumJsonDepth = 64;
        constexpr std::string_view EmptySha256 = "0000000000000000000000000000000000000000000000000000000000000000";

        class ArchiveFailure final
        {
          public:
            explicit ArchiveFailure(HubError error) : Error(std::move(error)) {}

            HubError Error;
        };

        [[nodiscard]] HubError ArchiveError(const HubErrorCode code, std::string message, std::string item = {},
                                            std::string details = {}, const bool retryable = false)
        {
            return {.Code = code,
                    .Message = std::move(message),
                    .Retryable = retryable,
                    .AffectedItem = std::move(item),
                    .TechnicalDetails = std::move(details)};
        }

        [[noreturn]] void Fail(const HubErrorCode code, std::string message, std::string item = {},
                               std::string details = {}, const bool retryable = false)
        {
            throw ArchiveFailure(
                ArchiveError(code, std::move(message), std::move(item), std::move(details), retryable));
        }

        [[nodiscard]] bool IsAbsoluteBoundedPath(const std::filesystem::path& path)
        {
            if (path.empty() || !path.is_absolute() || path == path.root_path())
                return false;
            try
            {
                if (path.generic_u8string().size() > 4096U)
                    return false;
            }
            catch (...)
            {
                return false;
            }
            return std::ranges::none_of(path, [](const std::filesystem::path& component)
                                        { return component == "." || component == ".."; });
        }

        [[nodiscard]] bool HasPackageExtension(const std::filesystem::path& path)
        {
            return path.extension() == ".keirepackage";
        }

        [[nodiscard]] std::string PathKey(const std::filesystem::path& path)
        {
            return Detail::PathToUtf8(path.lexically_normal());
        }

        [[nodiscard]] std::string CaseFoldedPath(const std::filesystem::path& path)
        {
            auto value = PathKey(path);
            std::ranges::transform(value, value.begin(), [](const unsigned char character)
                                   { return static_cast<char>(std::tolower(character)); });
            return value;
        }

        [[nodiscard]] std::filesystem::path NativeIoPath(const std::filesystem::path& path)
        {
#if defined(_WIN32)
            auto value = std::filesystem::absolute(path).lexically_normal().native();
            if (value.starts_with(LR"(\\?\)"))
                return value;
            if (value.starts_with(LR"(\\)"))
                return LR"(\\?\UNC\)" + value.substr(2);
            return LR"(\\?\)" + value;
#else
            return path;
#endif
        }

        [[nodiscard]] bool HasSymlinkAncestor(const std::filesystem::path& path)
        {
            auto current = path.lexically_normal();
            while (!current.empty())
            {
#if defined(_WIN32)
                const auto attributes = GetFileAttributesW(NativeIoPath(current).c_str());
                if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
                    return true;
                if (attributes == INVALID_FILE_ATTRIBUTES)
                {
                    const auto failure = GetLastError();
                    if (failure != ERROR_FILE_NOT_FOUND && failure != ERROR_PATH_NOT_FOUND)
                        return true;
                }
#else
                std::error_code error;
                const auto status = std::filesystem::symlink_status(NativeIoPath(current), error);
                if (!error && std::filesystem::is_symlink(status))
                    return true;
                if (error && error != std::errc::no_such_file_or_directory)
                    return true;
#endif
                if (current == current.root_path())
                    break;
                const auto parent = current.parent_path();
                if (parent == current)
                    break;
                current = parent;
            }
            return false;
        }

        [[nodiscard]] std::string_view PackageKindText(const PackageKind kind) noexcept
        {
            constexpr std::array names{"hubInstaller", "editor",          "buildSupport",
                                       "template",     "learningContent", "toolchain"};
            const auto index = static_cast<std::size_t>(kind);
            return index < names.size() ? names[index] : std::string_view{};
        }

        template <std::size_t RequiredCount, std::size_t OptionalCount>
        [[nodiscard]] bool HasObjectShape(const Detail::Json& value,
                                          const std::array<std::string_view, RequiredCount>& required,
                                          const std::array<std::string_view, OptionalCount>& optional)
        {
            if (!value.is_object())
                return false;
            for (const auto key : required)
            {
                if (!value.contains(std::string(key)))
                    return false;
            }
            for (const auto& [key, member] : value.items())
            {
                static_cast<void>(member);
                const auto permitted = [&](const auto& keys)
                { return std::ranges::find(keys, std::string_view(key)) != keys.end(); };
                if (!permitted(required) && !permitted(optional))
                    return false;
            }
            return true;
        }

        template <std::size_t RequiredCount>
        [[nodiscard]] bool HasObjectShape(const Detail::Json& value,
                                          const std::array<std::string_view, RequiredCount>& required)
        {
            return HasObjectShape(value, required, std::array<std::string_view, 0>{});
        }

        [[nodiscard]] HubResult<PackageManifest> CanonicalManifest(PackageManifest manifest)
        {
            manifest.ArtifactSizeBytes = 1;
            manifest.ArtifactSha256 = std::string(EmptySha256);
            if (const auto status = ValidatePackageManifest(manifest); !status)
                return HubResult<PackageManifest>::Failure(status.Error());
            if (manifest.Kind == PackageKind::HubInstaller ||
                manifest.Files.size() > PackageArchiveLimits::MaximumFiles ||
                manifest.InstalledSizeBytes > PackageArchiveLimits::MaximumPayloadBytes)
            {
                return HubResult<PackageManifest>::Failure(
                    ArchiveError(HubErrorCode::PackageManifestInvalid,
                                 "The package manifest is incompatible with the generic archive format.", manifest.Id));
            }
            for (const auto& file : manifest.Files)
            {
                const auto path = PathKey(file.Path);
                if (path.empty() || path.size() > PackageArchiveLimits::MaximumPathBytes ||
                    file.SizeBytes > PackageArchiveLimits::MaximumFileBytes)
                {
                    return HubResult<PackageManifest>::Failure(
                        ArchiveError(HubErrorCode::PackageManifestInvalid,
                                     "The package file inventory exceeds the archive limits.", manifest.Id, path));
                }
            }
            std::ranges::sort(manifest.Files, [](const PackageFile& left, const PackageFile& right)
                              { return PathKey(left.Path) < PathKey(right.Path); });
            std::ranges::sort(manifest.Dependencies,
                              [](const PackageDependency& left, const PackageDependency& right)
                              {
                                  if (left.PackageId != right.PackageId)
                                      return left.PackageId < right.PackageId;
                                  return left.Versions.ToString() < right.Versions.ToString();
                              });
            std::ranges::sort(manifest.Conflicts,
                              [](const PackageConflict& left, const PackageConflict& right)
                              {
                                  if (left.PackageId != right.PackageId)
                                      return left.PackageId < right.PackageId;
                                  return left.Versions.ToString() < right.Versions.ToString();
                              });
            std::ranges::sort(manifest.LicenseReferences);
            return HubResult<PackageManifest>::Success(std::move(manifest));
        }

        [[nodiscard]] Detail::Json EncodeManifestJson(const PackageManifest& manifest)
        {
            Detail::Json dependencies = Detail::Json::array();
            for (const auto& dependency : manifest.Dependencies)
            {
                dependencies.push_back(
                    {{"packageId", dependency.PackageId}, {"version", dependency.Versions.ToString()}});
            }
            Detail::Json conflicts = Detail::Json::array();
            for (const auto& conflict : manifest.Conflicts)
                conflicts.push_back({{"packageId", conflict.PackageId}, {"version", conflict.Versions.ToString()}});
            Detail::Json files = Detail::Json::array();
            for (const auto& file : manifest.Files)
            {
                files.push_back({{"path", PathKey(file.Path)},
                                 {"sizeBytes", file.SizeBytes},
                                 {"sha256", file.Sha256},
                                 {"mode", file.Mode}});
            }
            Detail::Json result{{"schemaVersion", manifest.SchemaVersion},
                                {"packageId", manifest.Id},
                                {"version", manifest.Version.ToString()},
                                {"type", PackageKindText(manifest.Kind)},
                                {"displayName", manifest.DisplayName},
                                {"channel", manifest.Channel},
                                {"platform", manifest.Platform},
                                {"architecture", manifest.Architecture},
                                {"installedSizeBytes", manifest.InstalledSizeBytes},
                                {"files", std::move(files)},
                                {"signatureKeyId", manifest.SignatureKeyId}};
            if (manifest.EngineCompatibility)
                result["engineCompatibility"] = manifest.EngineCompatibility->ToString();
            if (!dependencies.empty())
                result["dependencies"] = std::move(dependencies);
            if (!conflicts.empty())
                result["conflicts"] = std::move(conflicts);
            if (!manifest.LicenseReferences.empty())
                result["licenses"] = manifest.LicenseReferences;
            return result;
        }

        [[nodiscard]] HubResult<PackageManifest> ParseEmbeddedManifest(const std::span<const std::byte> exactBytes)
        {
            if (exactBytes.empty() || exactBytes.size() > PackageArchiveLimits::MaximumManifestBytes)
            {
                return HubResult<PackageManifest>::Failure(ArchiveError(
                    HubErrorCode::PackageManifestInvalid, "The embedded package manifest is empty or too large."));
            }
            const auto text = std::string_view(reinterpret_cast<const char*>(exactBytes.data()), exactBytes.size());
            auto parsed = Detail::ParseStrictJson(text, MaximumJsonDepth, HubErrorCode::PackageManifestInvalid,
                                                  "The embedded package manifest is malformed.", "package-manifest");
            if (!parsed)
                return HubResult<PackageManifest>::Failure(parsed.Error());
            try
            {
                constexpr std::array required{std::string_view{"schemaVersion"},
                                              std::string_view{"packageId"},
                                              std::string_view{"version"},
                                              std::string_view{"type"},
                                              std::string_view{"displayName"},
                                              std::string_view{"channel"},
                                              std::string_view{"platform"},
                                              std::string_view{"architecture"},
                                              std::string_view{"installedSizeBytes"},
                                              std::string_view{"files"},
                                              std::string_view{"signatureKeyId"}};
                constexpr std::array optional{std::string_view{"engineCompatibility"}, std::string_view{"dependencies"},
                                              std::string_view{"conflicts"}, std::string_view{"licenses"}};
                constexpr std::array fileKeys{std::string_view{"path"}, std::string_view{"sizeBytes"},
                                              std::string_view{"sha256"}, std::string_view{"mode"}};
                constexpr std::array relationRequired{std::string_view{"packageId"}};
                constexpr std::array relationOptional{std::string_view{"version"}};
                auto document = parsed.Value();
                if (!HasObjectShape(document, required, optional) || !document.at("files").is_array() ||
                    document.at("files").size() > PackageArchiveLimits::MaximumFiles)
                {
                    throw std::invalid_argument("The embedded manifest has an unexpected schema.");
                }
                for (const auto& file : document.at("files"))
                {
                    if (!HasObjectShape(file, fileKeys))
                        throw std::invalid_argument("An embedded file record has an unexpected schema.");
                }
                for (const auto key : {std::string_view{"dependencies"}, std::string_view{"conflicts"}})
                {
                    const auto collection = document.find(std::string(key));
                    if (collection == document.end())
                        continue;
                    if (!collection->is_array() || collection->size() > 128U)
                        throw std::invalid_argument("An embedded package relationship collection is invalid.");
                    for (const auto& relation : *collection)
                    {
                        if (!HasObjectShape(relation, relationRequired, relationOptional))
                            throw std::invalid_argument("An embedded package relationship is invalid.");
                    }
                }
                document["artifact"] = {{"sizeBytes", 1}, {"sha256", EmptySha256}};
                auto manifest = ParsePackageManifest(document.dump());
                if (!manifest)
                    return HubResult<PackageManifest>::Failure(manifest.Error());
                auto canonical = EncodePackageArchiveManifest(manifest.Value());
                if (!canonical)
                    return HubResult<PackageManifest>::Failure(canonical.Error());
                if (!std::ranges::equal(canonical.Value(), exactBytes))
                {
                    return HubResult<PackageManifest>::Failure(
                        ArchiveError(HubErrorCode::PackageManifestInvalid,
                                     "The embedded package manifest is not in canonical form.", manifest.Value().Id));
                }
                return manifest;
            }
            catch (const std::exception& error)
            {
                return HubResult<PackageManifest>::Failure(ArchiveError(
                    HubErrorCode::PackageManifestInvalid, "The embedded package manifest has an invalid schema.",
                    "package-manifest", error.what()));
            }
        }

        [[nodiscard]] bool IsValidSignature(const DetachedSignatureMetadata& signature)
        {
            const auto decoded = Detail::DecodeCanonicalBase64(signature.Signature, 64);
            return signature.Algorithm == "Ed25519" && Detail::IsDistributionKeyId(signature.KeyId) && decoded &&
                   decoded->size() == 64U;
        }

        [[nodiscard]] std::vector<std::byte> EncodeSignature(const DetachedSignatureMetadata& signature)
        {
            const auto text = Detail::Json{{"algorithm", signature.Algorithm},
                                           {"keyId", signature.KeyId},
                                           {"signature", signature.Signature}}
                                  .dump();
            return {reinterpret_cast<const std::byte*>(text.data()),
                    reinterpret_cast<const std::byte*>(text.data() + text.size())};
        }

        [[nodiscard]] HubResult<DetachedSignatureMetadata> ParseSignature(const std::span<const std::byte> exactBytes)
        {
            if (exactBytes.empty() || exactBytes.size() > PackageArchiveLimits::MaximumSignatureBytes)
            {
                return HubResult<DetachedSignatureMetadata>::Failure(
                    ArchiveError(HubErrorCode::CatalogSignatureInvalid,
                                 "The embedded package signature is empty or too large.", "package-signature"));
            }
            const auto text = std::string_view(reinterpret_cast<const char*>(exactBytes.data()), exactBytes.size());
            auto parsed = Detail::ParseStrictJson(text, MaximumJsonDepth, HubErrorCode::CatalogSignatureInvalid,
                                                  "The embedded package signature is malformed.", "package-signature");
            if (!parsed)
                return HubResult<DetachedSignatureMetadata>::Failure(parsed.Error());
            try
            {
                constexpr std::array keys{std::string_view{"algorithm"}, std::string_view{"keyId"},
                                          std::string_view{"signature"}};
                if (!HasObjectShape(parsed.Value(), keys))
                    throw std::invalid_argument("The signature object has an unexpected schema.");
                DetachedSignatureMetadata signature{.Algorithm = parsed.Value().at("algorithm").get<std::string>(),
                                                    .KeyId = parsed.Value().at("keyId").get<std::string>(),
                                                    .Signature = parsed.Value().at("signature").get<std::string>()};
                if (!IsValidSignature(signature) ||
                    EncodeSignature(signature) != std::vector<std::byte>(exactBytes.begin(), exactBytes.end()))
                {
                    throw std::invalid_argument("The signature metadata is invalid or non-canonical.");
                }
                return HubResult<DetachedSignatureMetadata>::Success(std::move(signature));
            }
            catch (const std::exception& error)
            {
                return HubResult<DetachedSignatureMetadata>::Failure(
                    ArchiveError(HubErrorCode::CatalogSignatureInvalid, "The embedded package signature is invalid.",
                                 "package-signature", error.what()));
            }
        }

        template <typename Integer>
        [[nodiscard]] std::array<std::byte, sizeof(Integer)> EncodeLittleEndian(Integer value)
        {
            std::array<std::byte, sizeof(Integer)> result{};
            for (std::size_t index = 0; index < result.size(); ++index)
            {
                result[index] = static_cast<std::byte>(value & 0xffU);
                if (index + 1U < result.size())
                    value = static_cast<Integer>(static_cast<std::uint64_t>(value) >> 8U);
            }
            return result;
        }

        template <typename Integer>
        [[nodiscard]] Integer DecodeLittleEndian(const std::span<const std::byte, sizeof(Integer)> bytes)
        {
            Integer result = 0;
            for (std::size_t index = 0; index < bytes.size(); ++index)
                result |= static_cast<Integer>(std::to_integer<unsigned>(bytes[index])) << (index * 8U);
            return result;
        }

        struct ContextDeleter final
        {
            void operator()(ZSTD_CCtx* context) const noexcept { ZSTD_freeCCtx(context); }
            void operator()(ZSTD_DCtx* context) const noexcept { ZSTD_freeDCtx(context); }
        };

        class CompressedWriter final
        {
          public:
            CompressedWriter(const std::filesystem::path& path, const int compressionLevel, std::string item)
                : m_Context(ZSTD_createCCtx()), m_Item(std::move(item))
            {
                auto output = Detail::ExclusivePackageOutput::Create(path, m_Item);
                if (!output)
                    throw ArchiveFailure(output.Error());
                m_Output = std::move(output).Value();
                if (!m_Context)
                    Fail(HubErrorCode::IoWrite, "The package archive compressor could not be created.", m_Item);
                const auto configured =
                    ZSTD_CCtx_setParameter(m_Context.get(), ZSTD_c_compressionLevel, compressionLevel);
                if (ZSTD_isError(configured))
                {
                    Fail(HubErrorCode::InvalidArgument, "The package compression level is invalid.", m_Item,
                         ZSTD_getErrorName(configured));
                }
            }

            void Write(const std::span<const std::byte> bytes)
            {
                ZSTD_inBuffer input{bytes.data(), bytes.size(), 0};
                while (input.pos < input.size)
                    static_cast<void>(Pump(input, ZSTD_e_continue));
            }

            template <typename Integer> void WriteInteger(const Integer value) { Write(EncodeLittleEndian(value)); }

            void Finish()
            {
                if (m_Finished)
                    return;
                ZSTD_inBuffer input{nullptr, 0, 0};
                std::size_t remaining = 1;
                while (remaining != 0)
                    remaining = Pump(input, ZSTD_e_end);
                if (const auto status = m_Output->Finish(); !status)
                    throw ArchiveFailure(status.Error());
                m_Digest = Detail::DigestToString(m_Hash.Finish());
                m_Finished = true;
            }

            void Publish(const std::filesystem::path& output)
            {
                if (const auto status = m_Output->Publish(output); !status)
                    throw ArchiveFailure(status.Error());
            }

            [[nodiscard]] std::uint64_t Size() const noexcept { return m_Size; }
            [[nodiscard]] const std::string& Digest() const noexcept { return m_Digest; }

          private:
            [[nodiscard]] std::size_t Pump(ZSTD_inBuffer& input, const ZSTD_EndDirective directive)
            {
                ZSTD_outBuffer output{m_Buffer.data(), m_Buffer.size(), 0};
                const auto result = ZSTD_compressStream2(m_Context.get(), &output, &input, directive);
                if (ZSTD_isError(result))
                {
                    Fail(HubErrorCode::IoWrite, "The package archive could not be compressed.", m_Item,
                         ZSTD_getErrorName(result));
                }
                if (m_Size > PackageArchiveLimits::MaximumArchiveBytes - output.pos)
                    Fail(HubErrorCode::IoWrite, "The package archive exceeds its byte limit.", m_Item);
                const auto bytes = std::as_bytes(std::span(m_Buffer.data(), static_cast<std::size_t>(output.pos)));
                if (const auto status = m_Output->Write(std::span(m_Buffer).first(output.pos)); !status)
                    throw ArchiveFailure(status.Error());
                m_Hash.Update(bytes);
                m_Size += output.pos;
                return result;
            }

            std::unique_ptr<Detail::ExclusivePackageOutput> m_Output;
            std::unique_ptr<ZSTD_CCtx, ContextDeleter> m_Context;
            std::vector<char> m_Buffer = std::vector<char>(BufferBytes);
            Detail::Sha256Builder m_Hash;
            std::string m_Item;
            std::string m_Digest;
            std::uint64_t m_Size = 0;
            bool m_Finished = false;
        };

        class CompressedReader final
        {
          public:
            explicit CompressedReader(const std::filesystem::path& path)
                : m_Input(NativeIoPath(path), std::ios::binary), m_Context(ZSTD_createDCtx()),
                  m_Item(Detail::PathToUtf8(path.filename()))
            {
                if (!m_Input || !m_Context)
                    Fail(HubErrorCode::IoRead, "The package archive could not be opened.", m_Item, {}, true);
            }

            void Read(const std::span<std::byte> destination)
            {
                std::size_t written = 0;
                while (written < destination.size())
                {
                    if (m_OutputPosition == m_OutputSize)
                        Fill();
                    const auto count = std::min(destination.size() - written, m_OutputSize - m_OutputPosition);
                    std::memcpy(destination.data() + written, m_Output.data() + m_OutputPosition, count);
                    written += count;
                    m_OutputPosition += count;
                }
            }

            template <typename Integer> [[nodiscard]] Integer ReadInteger()
            {
                std::array<std::byte, sizeof(Integer)> bytes{};
                Read(bytes);
                return DecodeLittleEndian<Integer>(bytes);
            }

            [[nodiscard]] std::vector<std::byte> ReadBytes(const std::size_t size)
            {
                std::vector<std::byte> result(size);
                Read(result);
                return result;
            }

            [[nodiscard]] std::string ReadText(const std::size_t size)
            {
                std::string result(size, '\0');
                Read(std::as_writable_bytes(std::span(result.data(), result.size())));
                return result;
            }

            void RequireEnd()
            {
                if (m_OutputPosition != m_OutputSize)
                    Fail(HubErrorCode::InvalidData, "The package archive contains undeclared payload data.", m_Item);
                while (!m_FrameEnded)
                {
                    Fill();
                    if (m_OutputSize != 0)
                        Fail(HubErrorCode::InvalidData, "The package archive contains undeclared payload data.",
                             m_Item);
                }
                if (m_Compressed.pos != m_Compressed.size || m_Input.peek() != std::ifstream::traits_type::eof())
                {
                    Fail(HubErrorCode::InvalidData, "The package archive contains trailing compressed data.", m_Item);
                }
                m_Digest = Detail::DigestToString(m_Hash.Finish());
            }

            [[nodiscard]] std::uint64_t Size() const noexcept { return m_CompressedBytes; }
            [[nodiscard]] const std::string& Digest() const noexcept { return m_Digest; }

          private:
            void Fill()
            {
                if (m_FrameEnded)
                    Fail(HubErrorCode::InvalidData, "The package archive is truncated.", m_Item);
                m_OutputPosition = 0;
                m_OutputSize = 0;
                while (m_OutputSize == 0 && !m_FrameEnded)
                {
                    if (m_Compressed.pos == m_Compressed.size)
                    {
                        m_Input.read(m_InputBuffer.data(), static_cast<std::streamsize>(m_InputBuffer.size()));
                        const auto count = m_Input.gcount();
                        if (count <= 0)
                            Fail(HubErrorCode::InvalidData, "The package archive is truncated.", m_Item);
                        const auto size = static_cast<std::size_t>(count);
                        if (m_CompressedBytes > PackageArchiveLimits::MaximumArchiveBytes - size)
                            Fail(HubErrorCode::InvalidData, "The package archive exceeds its byte limit.", m_Item);
                        const auto bytes = std::as_bytes(std::span(m_InputBuffer.data(), size));
                        m_Hash.Update(bytes);
                        m_CompressedBytes += size;
                        m_Compressed = {m_InputBuffer.data(), size, 0};
                    }
                    ZSTD_outBuffer output{m_Output.data(), m_Output.size(), 0};
                    const auto remaining = ZSTD_decompressStream(m_Context.get(), &output, &m_Compressed);
                    if (ZSTD_isError(remaining))
                    {
                        Fail(HubErrorCode::InvalidData, "The package archive could not be decompressed.", m_Item,
                             ZSTD_getErrorName(remaining));
                    }
                    m_OutputSize = output.pos;
                    m_FrameEnded = remaining == 0;
                }
            }

            std::ifstream m_Input;
            std::unique_ptr<ZSTD_DCtx, ContextDeleter> m_Context;
            std::vector<char> m_InputBuffer = std::vector<char>(BufferBytes);
            std::vector<std::byte> m_Output = std::vector<std::byte>(BufferBytes);
            ZSTD_inBuffer m_Compressed{nullptr, 0, 0};
            Detail::Sha256Builder m_Hash;
            std::string m_Item;
            std::string m_Digest;
            std::uint64_t m_CompressedBytes = 0;
            std::size_t m_OutputPosition = 0;
            std::size_t m_OutputSize = 0;
            bool m_FrameEnded = false;
        };

        struct ArchiveHeader final
        {
            PackageManifest Manifest;
            std::optional<DetachedSignatureMetadata> Signature;
            std::shared_ptr<const std::vector<std::byte>> ExactManifestBytes;
        };

        class OwnedDirectoryGuard final
        {
          public:
            explicit OwnedDirectoryGuard(std::filesystem::path path) : m_Path(std::move(path)) {}
            ~OwnedDirectoryGuard()
            {
                if (!m_Released)
                {
                    std::error_code ignored;
                    std::filesystem::remove_all(m_Path, ignored);
                }
            }

            void Release() noexcept { m_Released = true; }

          private:
            std::filesystem::path m_Path;
            bool m_Released = false;
        };

        [[nodiscard]] bool IsFilesystemSameOrWithin(const std::filesystem::path& root,
                                                    const std::filesystem::path& candidate)
        {
            auto current = candidate.lexically_normal();
            while (!current.empty())
            {
                std::error_code existsError;
                const bool exists = std::filesystem::exists(NativeIoPath(current), existsError);
                if (existsError && existsError != std::errc::no_such_file_or_directory)
                    return true;
                if (exists)
                {
                    std::error_code equivalentError;
                    const bool equivalent =
                        std::filesystem::equivalent(NativeIoPath(root), NativeIoPath(current), equivalentError);
                    if (equivalentError)
                        return true;
                    if (equivalent)
                        return true;
                }
                if (current == current.root_path())
                    break;
                const auto parent = current.parent_path();
                if (parent == current)
                    break;
                current = parent;
            }
            return false;
        }

        [[nodiscard]] bool IsRegularFileWithoutLinks(const std::filesystem::path& path)
        {
            if (HasSymlinkAncestor(path))
                return false;
            std::error_code error;
            const auto status = std::filesystem::symlink_status(NativeIoPath(path), error);
            return !error && status.type() == std::filesystem::file_type::regular;
        }

        [[nodiscard]] bool IsDirectoryWithoutLinks(const std::filesystem::path& path)
        {
            if (HasSymlinkAncestor(path))
                return false;
            std::error_code error;
            const auto status = std::filesystem::symlink_status(NativeIoPath(path), error);
            return !error && status.type() == std::filesystem::file_type::directory;
        }

        void ValidatePayloadTree(const std::filesystem::path& root, const PackageManifest& manifest)
        {
            if (!IsDirectoryWithoutLinks(root))
                Fail(HubErrorCode::InvalidArgument, "The package payload root is not a regular directory.",
                     manifest.Id);

            std::map<std::string, const PackageFile*, std::less<>> declared;
            for (const auto& file : manifest.Files)
                declared.emplace(PathKey(file.Path), &file);
            std::set<std::string, std::less<>> discovered;
            std::set<std::string, std::less<>> folded;
            std::uint64_t total = 0;
            for (std::filesystem::recursive_directory_iterator
                     iterator(NativeIoPath(root), std::filesystem::directory_options::none),
                 end;
                 iterator != end; ++iterator)
            {
                const auto status = iterator->symlink_status();
                if (std::filesystem::is_symlink(status))
                    Fail(HubErrorCode::PackageManifestInvalid, "Package payloads may not contain symbolic links.",
                         manifest.Id, Detail::PathToUtf8(iterator->path().filename()));
                if (std::filesystem::is_directory(status))
                    continue;
                if (!std::filesystem::is_regular_file(status))
                {
                    Fail(HubErrorCode::PackageManifestInvalid, "Package payloads may contain only regular files.",
                         manifest.Id, Detail::PathToUtf8(iterator->path().filename()));
                }
                const auto relative = iterator->path().lexically_relative(NativeIoPath(root)).lexically_normal();
                const auto key = PathKey(relative);
                if (!Detail::IsSafeRelativePath(relative) || key.size() > PackageArchiveLimits::MaximumPathBytes ||
                    !discovered.insert(key).second || !folded.insert(CaseFoldedPath(relative)).second)
                {
                    Fail(HubErrorCode::PackageManifestInvalid,
                         "The package payload contains an unsafe or colliding path.", manifest.Id, key);
                }
                const auto declaration = declared.find(key);
                if (declaration == declared.end())
                    Fail(HubErrorCode::PackageManifestInvalid, "The package payload contains an undeclared file.",
                         manifest.Id, key);
                const auto size = iterator->file_size();
                if (size != declaration->second->SizeBytes || total > PackageArchiveLimits::MaximumPayloadBytes - size)
                {
                    Fail(HubErrorCode::PackageManifestInvalid,
                         "A package payload file does not match its declared size.", manifest.Id, key);
                }
                total += size;
            }
            if (discovered.size() != declared.size() || total != manifest.InstalledSizeBytes)
            {
                Fail(HubErrorCode::PackageManifestInvalid,
                     "The package payload does not match its complete file inventory.", manifest.Id);
            }
        }

        void WritePayloadFile(CompressedWriter& writer, const std::filesystem::path& root, const PackageFile& file,
                              const std::string& packageId)
        {
            const auto path = root / file.Path;
            if (!IsRegularFileWithoutLinks(path))
                Fail(HubErrorCode::IoRead, "A declared package payload file is unavailable.", packageId,
                     PathKey(file.Path));
            std::ifstream input(NativeIoPath(path), std::ios::binary);
            if (!input)
                Fail(HubErrorCode::IoRead, "A declared package payload file could not be opened.", packageId,
                     PathKey(file.Path), true);

            const auto pathText = PathKey(file.Path);
            writer.WriteInteger(FileRecord);
            writer.WriteInteger(static_cast<std::uint32_t>(pathText.size()));
            writer.WriteInteger(file.SizeBytes);
            writer.Write(std::as_bytes(std::span(pathText.data(), pathText.size())));

            std::vector<std::byte> buffer(BufferBytes);
            Detail::Sha256Builder digest;
            std::uint64_t remaining = file.SizeBytes;
            while (remaining != 0)
            {
                const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, buffer.size()));
                input.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(count));
                if (input.gcount() != static_cast<std::streamsize>(count))
                {
                    Fail(HubErrorCode::IoRead, "A package payload file changed while it was archived.", packageId,
                         pathText);
                }
                const auto bytes = std::span(buffer).first(count);
                digest.Update(bytes);
                writer.Write(bytes);
                remaining -= count;
            }
            char extra = '\0';
            input.read(&extra, 1);
            if (input.gcount() != 0 || (!input.eof() && input.fail()))
                Fail(HubErrorCode::IoRead, "A package payload file changed while it was archived.", packageId,
                     pathText);
            if (Detail::DigestToString(digest.Finish()) != file.Sha256)
            {
                Fail(HubErrorCode::DownloadChecksumMismatch,
                     "A package payload file does not match its declared digest.", packageId, pathText);
            }
        }

        [[nodiscard]] ArchiveHeader ReadHeader(CompressedReader& reader, const std::uint64_t archiveSize,
                                               const PackageArchiveVerification& verification)
        {
            std::array<std::byte, ArchiveMagic.size()> magic{};
            reader.Read(magic);
            if (!std::ranges::equal(magic, std::as_bytes(std::span(ArchiveMagic))))
                Fail(HubErrorCode::InvalidData, "The package archive magic is invalid.", "package");
            if (reader.ReadInteger<std::uint32_t>() != ArchiveSchemaVersion)
                Fail(HubErrorCode::UnsupportedSchema, "The package archive schema is unsupported.", "package");
            const auto manifestSize = reader.ReadInteger<std::uint64_t>();
            const auto signatureSize = reader.ReadInteger<std::uint32_t>();
            if (manifestSize == 0 || manifestSize > PackageArchiveLimits::MaximumManifestBytes ||
                signatureSize > PackageArchiveLimits::MaximumSignatureBytes)
            {
                Fail(HubErrorCode::InvalidData, "The package archive header exceeds its limits.", "package");
            }

            auto exactManifest = reader.ReadBytes(static_cast<std::size_t>(manifestSize));
            auto parsedManifest = ParseEmbeddedManifest(exactManifest);
            if (!parsedManifest)
                throw ArchiveFailure(parsedManifest.Error());
            std::optional<DetachedSignatureMetadata> signature;
            if (signatureSize != 0)
            {
                auto parsedSignature = ParseSignature(reader.ReadBytes(signatureSize));
                if (!parsedSignature)
                    throw ArchiveFailure(parsedSignature.Error());
                signature = std::move(parsedSignature).Value();
                if (signature->KeyId != parsedManifest.Value().SignatureKeyId)
                {
                    Fail(HubErrorCode::CatalogIdentityMismatch,
                         "The package signature key does not match its embedded manifest.", parsedManifest.Value().Id);
                }
            }

            const bool catalog = verification.SignedCatalogManifest != nullptr;
            const bool offline = verification.OfflineTrustStore != nullptr;
            if (catalog == offline)
            {
                Fail(HubErrorCode::InvalidArgument,
                     "Package extraction requires exactly one catalog or offline trust source.",
                     parsedManifest.Value().Id);
            }
            if (catalog)
            {
                if (const auto status = ValidatePackageManifest(*verification.SignedCatalogManifest); !status)
                    throw ArchiveFailure(status.Error());
                auto expected = EncodePackageArchiveManifest(*verification.SignedCatalogManifest);
                if (!expected)
                    throw ArchiveFailure(expected.Error());
                if (!std::ranges::equal(expected.Value(), exactManifest))
                {
                    Fail(HubErrorCode::CatalogIdentityMismatch,
                         "The embedded package manifest does not match the signed catalog.", parsedManifest.Value().Id);
                }
                if (verification.SignedCatalogManifest->ArtifactSizeBytes != archiveSize)
                {
                    Fail(HubErrorCode::DownloadSizeMismatch,
                         "The package archive size does not match the signed catalog.", parsedManifest.Value().Id);
                }
                parsedManifest.Value().ArtifactSizeBytes = verification.SignedCatalogManifest->ArtifactSizeBytes;
                parsedManifest.Value().ArtifactSha256 = verification.SignedCatalogManifest->ArtifactSha256;
            }
            else
            {
                if (!signature)
                {
                    Fail(HubErrorCode::CatalogSignatureInvalid,
                         "Offline package imports require an embedded Ed25519 signature.", parsedManifest.Value().Id);
                }
                if (const auto status = verification.OfflineTrustStore->VerifyDetached(exactManifest, *signature,
                                                                                       parsedManifest.Value().Id);
                    !status)
                {
                    throw ArchiveFailure(status.Error());
                }
                parsedManifest.Value().ArtifactSizeBytes = archiveSize;
                parsedManifest.Value().ArtifactSha256 = std::string(EmptySha256);
            }

            return {.Manifest = std::move(parsedManifest).Value(),
                    .Signature = std::move(signature),
                    .ExactManifestBytes = std::make_shared<const std::vector<std::byte>>(std::move(exactManifest))};
        }

        void ThrowIfCancelled(const PackageArchiveCallbacks& callbacks, const std::string& item)
        {
            if (callbacks.Cancelled && callbacks.Cancelled())
            {
                Fail(HubErrorCode::WorkerInterrupted, "Package extraction was cancelled.", item, {}, true);
            }
        }

        void Report(const PackageArchiveCallbacks& callbacks, const std::uint64_t completed, const std::uint64_t total,
                    const std::string_view file, const std::string& item)
        {
            if (!callbacks.Progress)
                return;
            try
            {
                callbacks.Progress(
                    {.CompletedBytes = completed, .TotalBytes = total, .CurrentFile = std::string(file)});
            }
            catch (const std::exception& error)
            {
                Fail(HubErrorCode::WorkerProtocolInvalid, "The package extraction progress callback failed.", item,
                     error.what());
            }
            catch (...)
            {
                Fail(HubErrorCode::WorkerProtocolInvalid, "The package extraction progress callback failed.", item);
            }
        }

        void ApplyFileMode(const std::filesystem::path& path, const PackageFile& file)
        {
#if !defined(_WIN32)
            const auto permissions = file.Mode == 0755U
                                         ? std::filesystem::perms::owner_all | std::filesystem::perms::group_read |
                                               std::filesystem::perms::group_exec |
                                               std::filesystem::perms::others_read | std::filesystem::perms::others_exec
                                         : std::filesystem::perms::owner_read | std::filesystem::perms::owner_write |
                                               std::filesystem::perms::group_read | std::filesystem::perms::others_read;
            std::error_code error;
            std::filesystem::permissions(NativeIoPath(path), permissions, std::filesystem::perm_options::replace,
                                         error);
            if (error)
                Fail(HubErrorCode::IoWrite, "A staged package file mode could not be applied.", PathKey(file.Path),
                     error.message(), true);
#else
            static_cast<void>(path);
            static_cast<void>(file);
#endif
        }

        void ExtractFile(CompressedReader& reader, const std::filesystem::path& staging, const PackageFile& file,
                         const PackageArchiveCallbacks& callbacks, std::uint64_t& completed, const std::uint64_t total,
                         const std::string& packageId)
        {
            const auto kind = reader.ReadInteger<std::uint8_t>();
            if (kind == EndRecord)
                Fail(HubErrorCode::InvalidData, "The package archive is missing a declared file.", packageId,
                     PathKey(file.Path));
            if (kind != FileRecord)
            {
                Fail(HubErrorCode::InvalidData, "The package archive contains a symbolic link or unsupported record.",
                     packageId);
            }
            const auto pathSize = reader.ReadInteger<std::uint32_t>();
            const auto size = reader.ReadInteger<std::uint64_t>();
            if (pathSize == 0 || pathSize > PackageArchiveLimits::MaximumPathBytes ||
                size > PackageArchiveLimits::MaximumFileBytes)
            {
                Fail(HubErrorCode::InvalidData, "A package archive file record exceeds its limits.", packageId);
            }
            const auto recordPath = reader.ReadText(pathSize);
            std::filesystem::path decodedPath;
            try
            {
                decodedPath = Detail::PathFromUtf8(recordPath);
            }
            catch (const std::exception& error)
            {
                Fail(HubErrorCode::InvalidData, "A package archive file path is invalid.", packageId, error.what());
            }
            if (!Detail::IsSafeRelativePath(decodedPath) || recordPath != PathKey(file.Path) || size != file.SizeBytes)
            {
                Fail(HubErrorCode::InvalidData, "A package archive file record does not match its declaration.",
                     packageId, recordPath);
            }

            const auto target = staging / file.Path;
            std::error_code error;
            std::filesystem::create_directories(NativeIoPath(target.parent_path()), error);
            if (error || !IsDirectoryWithoutLinks(target.parent_path()))
            {
                Fail(HubErrorCode::IoWrite, "A staged package directory could not be created.", packageId, recordPath,
                     true);
            }
            auto output = Detail::ExclusivePackageOutput::Create(target, packageId);
            if (!output)
                throw ArchiveFailure(output.Error());

            std::vector<std::byte> buffer(BufferBytes);
            Detail::Sha256Builder digest;
            std::uint64_t remaining = file.SizeBytes;
            while (remaining != 0)
            {
                ThrowIfCancelled(callbacks, packageId);
                const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, buffer.size()));
                const auto bytes = std::span(buffer).first(count);
                reader.Read(bytes);
                const auto characters =
                    std::span(reinterpret_cast<const char*>(bytes.data()), static_cast<std::size_t>(bytes.size()));
                if (const auto status = output.Value()->Write(characters); !status)
                    throw ArchiveFailure(status.Error());
                digest.Update(bytes);
                remaining -= count;
                completed += count;
                Report(callbacks, completed, total, recordPath, packageId);
            }
            if (Detail::DigestToString(digest.Finish()) != file.Sha256)
            {
                Fail(HubErrorCode::DownloadChecksumMismatch, "A package archive file failed digest verification.",
                     packageId, recordPath);
            }
            if (const auto status = output.Value()->Finish(); !status)
                throw ArchiveFailure(status.Error());
            if (const auto status = output.Value()->Publish(target); !status)
                throw ArchiveFailure(status.Error());
            ApplyFileMode(target, file);
        }

        void ValidateExtractedTree(const std::filesystem::path& staging, const PackageManifest& manifest)
        {
#if !defined(_WIN32)
            constexpr auto specialPermissions =
                std::filesystem::perms::set_uid | std::filesystem::perms::set_gid | std::filesystem::perms::sticky_bit;
            constexpr auto directoryPermissions =
                std::filesystem::perms::owner_all | std::filesystem::perms::group_read |
                std::filesystem::perms::group_exec | std::filesystem::perms::others_read |
                std::filesystem::perms::others_exec;
            std::error_code rootError;
            const auto rootStatus = std::filesystem::symlink_status(NativeIoPath(staging), rootError);
            if (rootError || (rootStatus.permissions() & specialPermissions) != std::filesystem::perms::none ||
                (rootStatus.permissions() & std::filesystem::perms::mask) != directoryPermissions)
                Fail(HubErrorCode::InvalidData, "The staged package root has unsafe permissions.", manifest.Id);
#endif
            std::map<std::string, const PackageFile*, std::less<>> declared;
            for (const auto& file : manifest.Files)
                declared.emplace(PathKey(file.Path), &file);
            std::set<std::string, std::less<>> found;
            std::set<std::string, std::less<>> folded;
            std::uint64_t total = 0;
            for (std::filesystem::recursive_directory_iterator
                     iterator(NativeIoPath(staging), std::filesystem::directory_options::none),
                 end;
                 iterator != end; ++iterator)
            {
                const auto status = iterator->symlink_status();
                if (std::filesystem::is_symlink(status))
                    Fail(HubErrorCode::InvalidData, "The staged package contains a symbolic link.", manifest.Id);
                if (std::filesystem::is_directory(status))
                {
#if !defined(_WIN32)
                    if ((status.permissions() & specialPermissions) != std::filesystem::perms::none ||
                        (status.permissions() & std::filesystem::perms::mask) != directoryPermissions)
                        Fail(HubErrorCode::InvalidData, "A staged package directory has unsafe permissions.",
                             manifest.Id);
#endif
                    continue;
                }
                if (!std::filesystem::is_regular_file(status))
                    Fail(HubErrorCode::InvalidData, "The staged package contains an unsupported file.", manifest.Id);
                const auto relative = iterator->path().lexically_relative(NativeIoPath(staging)).lexically_normal();
                const auto key = PathKey(relative);
                const auto declaration = declared.find(key);
                if (!Detail::IsSafeRelativePath(relative) || declaration == declared.end() ||
                    !found.insert(key).second || !folded.insert(CaseFoldedPath(relative)).second)
                {
                    Fail(HubErrorCode::InvalidData, "The staged package contains an undeclared or colliding file.",
                         manifest.Id, key);
                }
                const auto size = iterator->file_size();
                if (size != declaration->second->SizeBytes || total > PackageArchiveLimits::MaximumPayloadBytes - size)
                    Fail(HubErrorCode::InvalidData, "The staged package inventory exceeds its limit.", manifest.Id);
                total += size;

                std::ifstream input(NativeIoPath(iterator->path()), std::ios::binary);
                if (!input)
                    Fail(HubErrorCode::IoRead, "A staged package file could not be verified.", manifest.Id, key);
                std::vector<std::byte> buffer(BufferBytes);
                Detail::Sha256Builder digest;
                std::uint64_t remaining = size;
                while (remaining != 0)
                {
                    const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, buffer.size()));
                    input.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(count));
                    if (input.gcount() != static_cast<std::streamsize>(count))
                        Fail(HubErrorCode::IoRead, "A staged package file changed during verification.", manifest.Id,
                             key);
                    digest.Update(std::span(buffer).first(count));
                    remaining -= count;
                }
                char extra = '\0';
                input.read(&extra, 1);
                if (input.gcount() != 0 || (!input.eof() && input.fail()))
                    Fail(HubErrorCode::IoRead, "A staged package file changed during verification.", manifest.Id, key);
                if (Detail::DigestToString(digest.Finish()) != declaration->second->Sha256)
                {
                    Fail(HubErrorCode::DownloadChecksumMismatch, "A staged package file failed digest verification.",
                         manifest.Id, key);
                }
#if !defined(_WIN32)
                const auto expectedMode =
                    declaration->second->Mode == 0755U
                        ? std::filesystem::perms::owner_all | std::filesystem::perms::group_read |
                              std::filesystem::perms::group_exec | std::filesystem::perms::others_read |
                              std::filesystem::perms::others_exec
                        : std::filesystem::perms::owner_read | std::filesystem::perms::owner_write |
                              std::filesystem::perms::group_read | std::filesystem::perms::others_read;
                if ((status.permissions() & std::filesystem::perms::mask) != expectedMode)
                    Fail(HubErrorCode::InvalidData, "A staged package file mode changed.", manifest.Id, key);
#endif
            }
            if (found.size() != declared.size() || total != manifest.InstalledSizeBytes)
            {
                Fail(HubErrorCode::InvalidData, "The staged package does not match its complete inventory.",
                     manifest.Id);
            }
        }

        void NormalizeStagingDirectoryModes(const std::filesystem::path& staging, const std::string& packageId)
        {
#if !defined(_WIN32)
            constexpr auto permissions = std::filesystem::perms::owner_all | std::filesystem::perms::group_read |
                                         std::filesystem::perms::group_exec | std::filesystem::perms::others_read |
                                         std::filesystem::perms::others_exec;
            std::error_code error;
            std::filesystem::permissions(NativeIoPath(staging), permissions, std::filesystem::perm_options::replace,
                                         error);
            if (error)
                Fail(HubErrorCode::IoWrite, "The package staging root permissions could not be normalized.", packageId,
                     error.message(), true);
            for (std::filesystem::recursive_directory_iterator iterator(NativeIoPath(staging)), end; iterator != end;
                 ++iterator)
            {
                const auto status = iterator->symlink_status();
                if (std::filesystem::is_symlink(status))
                    Fail(HubErrorCode::InvalidData, "The package staging tree contains a symbolic link.", packageId);
                if (!std::filesystem::is_directory(status))
                    continue;
                std::filesystem::permissions(iterator->path(), permissions, std::filesystem::perm_options::replace,
                                             error);
                if (error)
                    Fail(HubErrorCode::IoWrite, "A package staging directory mode could not be normalized.", packageId,
                         error.message(), true);
            }
#else
            static_cast<void>(staging);
            static_cast<void>(packageId);
#endif
        }

        [[nodiscard]] HubError UnexpectedFailure(const HubErrorCode code, const std::string_view message,
                                                 const std::filesystem::path& path, const std::exception& error)
        {
            return ArchiveError(code, std::string(message), Detail::PathToUtf8(path.filename()), error.what(), true);
        }
    } // namespace

    HubResult<std::vector<std::byte>> EncodePackageArchiveManifest(const PackageManifest& manifest)
    {
        try
        {
            auto canonical = CanonicalManifest(manifest);
            if (!canonical)
                return HubResult<std::vector<std::byte>>::Failure(canonical.Error());
            const auto text = EncodeManifestJson(canonical.Value()).dump();
            if (text.empty() || text.size() > PackageArchiveLimits::MaximumManifestBytes)
            {
                return HubResult<std::vector<std::byte>>::Failure(
                    ArchiveError(HubErrorCode::PackageManifestInvalid,
                                 "The canonical package manifest exceeds its byte limit.", manifest.Id));
            }
            return HubResult<std::vector<std::byte>>::Success(
                {reinterpret_cast<const std::byte*>(text.data()),
                 reinterpret_cast<const std::byte*>(text.data() + text.size())});
        }
        catch (const ArchiveFailure& failure)
        {
            return HubResult<std::vector<std::byte>>::Failure(failure.Error);
        }
        catch (const std::exception& error)
        {
            return HubResult<std::vector<std::byte>>::Failure(
                ArchiveError(HubErrorCode::PackageManifestInvalid,
                             "The canonical package manifest could not be encoded.", manifest.Id, error.what()));
        }
    }

    HubStatus ValidatePackageTree(const std::filesystem::path& root, const PackageManifest& manifest)
    {
        try
        {
            const auto normalized = root.lexically_normal();
            if (!IsAbsoluteBoundedPath(normalized) || !IsDirectoryWithoutLinks(normalized))
            {
                return HubStatus::Failure(ArchiveError(HubErrorCode::UnsafeInstallRoot,
                                                       "The package tree is outside its safe filesystem boundary.",
                                                       manifest.Id));
            }
            if (const auto status = ValidatePackageManifest(manifest); !status)
                return status;
            ValidateExtractedTree(normalized, manifest);
            return HubStatus::Success();
        }
        catch (const ArchiveFailure& failure)
        {
            return HubStatus::Failure(failure.Error);
        }
        catch (const std::exception& error)
        {
            return HubStatus::Failure(
                UnexpectedFailure(HubErrorCode::InvalidData, "The package tree could not be verified.", root, error));
        }
    }

    HubResult<PackageArchiveMetadata> WritePackageArchive(const PackageArchiveWriteRequest& request)
    {
        auto output = request.Output.lexically_normal();
        try
        {
            const auto root = request.PayloadRoot.lexically_normal();
            if (!IsAbsoluteBoundedPath(root) || !IsAbsoluteBoundedPath(output) || !HasPackageExtension(output) ||
                IsFilesystemSameOrWithin(root, output) || request.CompressionLevel < ZSTD_minCLevel() ||
                request.CompressionLevel > ZSTD_maxCLevel())
            {
                return HubResult<PackageArchiveMetadata>::Failure(
                    ArchiveError(HubErrorCode::InvalidArgument, "The package archive write request is invalid.",
                                 request.Manifest.Id));
            }
            std::error_code error;
            if (!IsDirectoryWithoutLinks(output.parent_path()) ||
                std::filesystem::exists(NativeIoPath(output), error) || error)
            {
                return HubResult<PackageArchiveMetadata>::Failure(
                    ArchiveError(HubErrorCode::DestinationConflict, "The package archive destination is unavailable.",
                                 Detail::PathToUtf8(output.filename()), error.message()));
            }
            auto canonical = CanonicalManifest(request.Manifest);
            if (!canonical)
                return HubResult<PackageArchiveMetadata>::Failure(canonical.Error());
            auto exactManifest = EncodePackageArchiveManifest(canonical.Value());
            if (!exactManifest)
                return HubResult<PackageArchiveMetadata>::Failure(exactManifest.Error());
            if (request.EmbeddedSignature && (!IsValidSignature(*request.EmbeddedSignature) ||
                                              request.EmbeddedSignature->KeyId != canonical.Value().SignatureKeyId))
            {
                return HubResult<PackageArchiveMetadata>::Failure(
                    ArchiveError(HubErrorCode::CatalogSignatureInvalid,
                                 "The embedded package signature metadata is invalid.", canonical.Value().Id));
            }
            const auto signatureBytes =
                request.EmbeddedSignature ? EncodeSignature(*request.EmbeddedSignature) : std::vector<std::byte>{};
            if (signatureBytes.size() > PackageArchiveLimits::MaximumSignatureBytes)
            {
                return HubResult<PackageArchiveMetadata>::Failure(
                    ArchiveError(HubErrorCode::CatalogSignatureInvalid,
                                 "The embedded package signature exceeds its byte limit.", canonical.Value().Id));
            }
            ValidatePayloadTree(root, canonical.Value());
            CompressedWriter writer(output, request.CompressionLevel, Detail::PathToUtf8(output.filename()));
            writer.Write(std::as_bytes(std::span(ArchiveMagic)));
            writer.WriteInteger(ArchiveSchemaVersion);
            writer.WriteInteger(static_cast<std::uint64_t>(exactManifest.Value().size()));
            writer.WriteInteger(static_cast<std::uint32_t>(signatureBytes.size()));
            writer.Write(exactManifest.Value());
            writer.Write(signatureBytes);
            for (const auto& file : canonical.Value().Files)
                WritePayloadFile(writer, root, file, canonical.Value().Id);
            writer.WriteInteger(EndRecord);
            writer.WriteInteger(static_cast<std::uint64_t>(canonical.Value().Files.size()));
            writer.WriteInteger(canonical.Value().InstalledSizeBytes);
            writer.Finish();
            if (writer.Size() == 0 || writer.Size() > PackageArchiveLimits::MaximumArchiveBytes)
                Fail(HubErrorCode::IoWrite, "The package archive output has an invalid size.", canonical.Value().Id);
            canonical.Value().ArtifactSizeBytes = writer.Size();
            canonical.Value().ArtifactSha256 = writer.Digest();
            if (const auto status = ValidatePackageManifest(canonical.Value()); !status)
                return HubResult<PackageArchiveMetadata>::Failure(status.Error());
            auto result = HubResult<PackageArchiveMetadata>::Success(
                {.Manifest = std::move(canonical).Value(),
                 .EmbeddedSignature = request.EmbeddedSignature,
                 .ExactManifestBytes = std::make_shared<const std::vector<std::byte>>(std::move(exactManifest).Value()),
                 .ArchiveSizeBytes = writer.Size(),
                 .ArchiveSha256 = writer.Digest()});
            writer.Publish(output);
            return result;
        }
        catch (const ArchiveFailure& failure)
        {
            return HubResult<PackageArchiveMetadata>::Failure(failure.Error);
        }
        catch (const std::exception& error)
        {
            return HubResult<PackageArchiveMetadata>::Failure(
                UnexpectedFailure(HubErrorCode::IoWrite, "The package archive could not be written.", output, error));
        }
    }

    HubResult<PackageArchiveExtraction> ExtractPackageArchiveToStaging(const std::filesystem::path& archive,
                                                                       const std::filesystem::path& stagingRoot,
                                                                       const PackageArchiveVerification& verification,
                                                                       const PackageArchiveCallbacks& callbacks)
    {
        const auto source = archive.lexically_normal();
        const auto staging = stagingRoot.lexically_normal();
        const auto allowedParent = verification.AllowedStagingParent.lexically_normal();
        try
        {
            const bool isCatalogCache =
                verification.SignedCatalogManifest != nullptr && source.extension() == ".package";
            if (!IsAbsoluteBoundedPath(source) || (!HasPackageExtension(source) && !isCatalogCache) ||
                !IsAbsoluteBoundedPath(staging) || !IsAbsoluteBoundedPath(allowedParent) ||
                staging.parent_path() != allowedParent ||
                !Detail::PathToUtf8(staging.filename()).starts_with(".keire-stage-") ||
                (verification.SignedCatalogManifest == nullptr) == (verification.OfflineTrustStore == nullptr))
            {
                return HubResult<PackageArchiveExtraction>::Failure(
                    ArchiveError(HubErrorCode::InvalidArgument, "The package archive extraction request is invalid.",
                                 Detail::PathToUtf8(source.filename())));
            }
            std::error_code error;
            if (!IsRegularFileWithoutLinks(source))
            {
                return HubResult<PackageArchiveExtraction>::Failure(
                    ArchiveError(HubErrorCode::IoRead, "The package archive is unavailable.",
                                 Detail::PathToUtf8(source.filename()), {}, true));
            }
            const auto archiveSize = std::filesystem::file_size(NativeIoPath(source), error);
            if (error || archiveSize == 0 || archiveSize > PackageArchiveLimits::MaximumArchiveBytes)
            {
                return HubResult<PackageArchiveExtraction>::Failure(
                    ArchiveError(HubErrorCode::InvalidData, "The package archive size is invalid.",
                                 Detail::PathToUtf8(source.filename()), error.message()));
            }
            if (!IsDirectoryWithoutLinks(allowedParent) || std::filesystem::exists(NativeIoPath(staging), error) ||
                error)
            {
                return HubResult<PackageArchiveExtraction>::Failure(
                    ArchiveError(HubErrorCode::UnsafeInstallRoot, "The package staging boundary is unavailable.",
                                 Detail::PathToUtf8(staging.filename()), error.message()));
            }

            ThrowIfCancelled(callbacks, Detail::PathToUtf8(source.filename()));
            CompressedReader reader(source);
            auto header = ReadHeader(reader, archiveSize, verification);
            if (!std::filesystem::create_directory(NativeIoPath(staging), error) || error)
            {
                Fail(HubErrorCode::IoWrite, "The package staging directory could not be created.", header.Manifest.Id,
                     error.message(), true);
            }
            OwnedDirectoryGuard cleanup(staging);
            if (!IsDirectoryWithoutLinks(staging))
                Fail(HubErrorCode::UnsafeInstallRoot, "The package staging directory is unsafe.", header.Manifest.Id);
            std::uint64_t completed = 0;
            Report(callbacks, completed, header.Manifest.InstalledSizeBytes, {}, header.Manifest.Id);
            for (const auto& file : header.Manifest.Files)
            {
                ThrowIfCancelled(callbacks, header.Manifest.Id);
                ExtractFile(reader, staging, file, callbacks, completed, header.Manifest.InstalledSizeBytes,
                            header.Manifest.Id);
            }
            const auto end = reader.ReadInteger<std::uint8_t>();
            if (end != EndRecord)
            {
                Fail(HubErrorCode::InvalidData,
                     end == FileRecord ? "The package archive contains an undeclared or duplicate file."
                                       : "The package archive contains an unsupported record.",
                     header.Manifest.Id);
            }
            const auto fileCount = reader.ReadInteger<std::uint64_t>();
            const auto payloadBytes = reader.ReadInteger<std::uint64_t>();
            if (fileCount != header.Manifest.Files.size() || payloadBytes != header.Manifest.InstalledSizeBytes ||
                completed != payloadBytes)
            {
                Fail(HubErrorCode::InvalidData, "The package archive inventory totals do not match its manifest.",
                     header.Manifest.Id);
            }
            reader.RequireEnd();
            if (reader.Size() != archiveSize)
                Fail(HubErrorCode::DownloadSizeMismatch, "The package archive byte count changed during extraction.",
                     header.Manifest.Id);
            if (verification.SignedCatalogManifest &&
                reader.Digest() != verification.SignedCatalogManifest->ArtifactSha256)
            {
                Fail(HubErrorCode::DownloadChecksumMismatch,
                     "The package archive does not match the signed catalog digest.", header.Manifest.Id);
            }
            header.Manifest.ArtifactSizeBytes = archiveSize;
            header.Manifest.ArtifactSha256 = reader.Digest();
            if (const auto status = ValidatePackageManifest(header.Manifest); !status)
                throw ArchiveFailure(status.Error());
            NormalizeStagingDirectoryModes(staging, header.Manifest.Id);
            ValidateExtractedTree(staging, header.Manifest);
            Report(callbacks, completed, header.Manifest.InstalledSizeBytes, {}, header.Manifest.Id);
            auto result = HubResult<PackageArchiveExtraction>::Success(
                {.Metadata = {.Manifest = std::move(header.Manifest),
                              .EmbeddedSignature = std::move(header.Signature),
                              .ExactManifestBytes = std::move(header.ExactManifestBytes),
                              .ArchiveSizeBytes = archiveSize,
                              .ArchiveSha256 = reader.Digest()},
                 .StagingRoot = staging});
            cleanup.Release();
            return result;
        }
        catch (const ArchiveFailure& failure)
        {
            return HubResult<PackageArchiveExtraction>::Failure(failure.Error);
        }
        catch (const std::exception& error)
        {
            return HubResult<PackageArchiveExtraction>::Failure(UnexpectedFailure(
                HubErrorCode::InvalidData, "The package archive could not be extracted.", source, error));
        }
    }
} // namespace KeireHub
