#include "KeireInternal/Build/PlayerPackage.h"

#include "KeireInternal/Assets/AssetInternal.h"
#include "KeireInternal/FileSystem.h"
#include "KeireInternal/Process.h"

#include <nlohmann/json.hpp>
#include <stb_image.h>
#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <map>
#include <ranges>
#include <set>
#include <stdexcept>
#include <system_error>
#include <thread>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace Keire::Detail
{
    namespace
    {
        using Json = nlohmann::json;

        constexpr std::uintmax_t MaximumPlayerBytes = 16ULL * 1024ULL * 1024ULL * 1024ULL;
        constexpr std::size_t MaximumPlayerFiles = 32768;
        constexpr std::size_t MaximumDescriptorBytes = 64U * 1024U;
        constexpr std::size_t MaximumIconSourceBytes = 64U * 1024U * 1024U;
        constexpr std::size_t MaximumEncodedIconBytes = 32U * 1024U * 1024U;

        struct BrandingImage final
        {
            std::uint32_t Width = 0;
            std::uint32_t Height = 0;
            std::vector<std::uint8_t> Pixels;
        };

        template <typename Integer> void AppendLittleEndian(std::vector<std::byte>& output, Integer value)
        {
            for (std::size_t index = 0; index < sizeof(Integer); ++index)
            {
                output.push_back(static_cast<std::byte>(value & 0xffU));
                value >>= 8U;
            }
        }

        void AppendBigEndian(std::vector<std::byte>& output, const std::uint32_t value)
        {
            output.push_back(static_cast<std::byte>((value >> 24U) & 0xffU));
            output.push_back(static_cast<std::byte>((value >> 16U) & 0xffU));
            output.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
            output.push_back(static_cast<std::byte>(value & 0xffU));
        }

        [[nodiscard]] BrandingImage BuiltinPlayerIcon()
        {
            constexpr std::uint32_t size = 512;
            BrandingImage result{.Width = size, .Height = size, .Pixels = std::vector<std::uint8_t>(size * size * 4U)};
            for (std::uint32_t y = 0; y < size; ++y)
            {
                for (std::uint32_t x = 0; x < size; ++x)
                {
                    const auto index = static_cast<std::size_t>(y * size + x) * 4U;
                    result.Pixels[index] = static_cast<std::uint8_t>(32U + x * 34U / size);
                    result.Pixels[index + 1] = static_cast<std::uint8_t>(91U + y * 44U / size);
                    result.Pixels[index + 2] = static_cast<std::uint8_t>(196U + x * 40U / size);
                    result.Pixels[index + 3] = 255U;
                    const auto stroke = size / 13U;
                    const bool stem =
                        x > size * 3U / 10U && x < size * 3U / 10U + stroke && y > size / 5U && y < size * 4U / 5U;
                    const auto upper = static_cast<std::int32_t>(size / 2U) - static_cast<std::int32_t>(y);
                    const auto lower = static_cast<std::int32_t>(y) - static_cast<std::int32_t>(size / 2U);
                    const bool diagonal = x >= size * 3U / 10U && x < size * 7U / 10U &&
                                          (std::abs(static_cast<std::int32_t>(x - size * 3U / 10U) - upper) <
                                               static_cast<std::int32_t>(stroke) ||
                                           std::abs(static_cast<std::int32_t>(x - size * 3U / 10U) - lower) <
                                               static_cast<std::int32_t>(stroke));
                    if (stem || diagonal)
                        result.Pixels[index] = result.Pixels[index + 1] = result.Pixels[index + 2] = 244U;
                }
            }
            return result;
        }

        [[nodiscard]] BrandingImage DecodePlayerIcon(const std::span<const std::byte> source)
        {
            if (source.empty())
                return BuiltinPlayerIcon();
            if (source.size() > MaximumIconSourceBytes)
                throw std::runtime_error("Player icon source exceeds its size limit.");
            int width = 0;
            int height = 0;
            int channels = 0;
            auto* decoded = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(source.data()),
                                                  static_cast<int>(source.size()), &width, &height, &channels, 4);
            if (!decoded || width <= 0 || height <= 0 || width > 8192 || height > 8192)
            {
                const auto diagnostic = stbi_failure_reason() ? std::string(stbi_failure_reason()) : "unknown error";
                stbi_image_free(decoded);
                throw std::runtime_error("Player icon could not be decoded: " + diagnostic);
            }
            BrandingImage result{.Width = static_cast<std::uint32_t>(width),
                                 .Height = static_cast<std::uint32_t>(height),
                                 .Pixels = std::vector<std::uint8_t>(decoded, decoded + width * height * 4)};
            stbi_image_free(decoded);
            return result;
        }

        [[nodiscard]] BrandingImage ResizePlayerIcon(const BrandingImage& source, const std::uint32_t size)
        {
            BrandingImage result{.Width = size, .Height = size, .Pixels = std::vector<std::uint8_t>(size * size * 4U)};
            const auto scale =
                std::min(static_cast<double>(size) / source.Width, static_cast<double>(size) / source.Height);
            const auto width = std::max(1U, static_cast<std::uint32_t>(source.Width * scale));
            const auto height = std::max(1U, static_cast<std::uint32_t>(source.Height * scale));
            const auto offsetX = (size - width) / 2U;
            const auto offsetY = (size - height) / 2U;
            for (std::uint32_t y = 0; y < height; ++y)
            {
                for (std::uint32_t x = 0; x < width; ++x)
                {
                    const auto sourceX = std::min(source.Width - 1U, x * source.Width / width);
                    const auto sourceY = std::min(source.Height - 1U, y * source.Height / height);
                    const auto sourceIndex = static_cast<std::size_t>(sourceY * source.Width + sourceX) * 4U;
                    const auto destinationIndex = static_cast<std::size_t>((y + offsetY) * size + x + offsetX) * 4U;
                    std::copy_n(source.Pixels.begin() + static_cast<std::ptrdiff_t>(sourceIndex), 4,
                                result.Pixels.begin() + static_cast<std::ptrdiff_t>(destinationIndex));
                }
            }
            return result;
        }

        [[nodiscard]] std::vector<std::byte> EncodePng(const BrandingImage& image)
        {
            struct PngOutput final
            {
                std::vector<std::byte> Bytes;
                bool Failed = false;
            };
            PngOutput result;
            const auto callback = [](void* context, void* data, const int size)
            {
                auto& output = *static_cast<PngOutput*>(context);
                if (size < 0 || output.Bytes.size() > MaximumEncodedIconBytes - static_cast<std::size_t>(size))
                {
                    output.Failed = true;
                    return;
                }
                const auto* bytes = static_cast<const std::byte*>(data);
                output.Bytes.insert(output.Bytes.end(), bytes, bytes + size);
            };
            if (stbi_write_png_to_func(callback, &result, static_cast<int>(image.Width), static_cast<int>(image.Height),
                                       4, image.Pixels.data(), static_cast<int>(image.Width * 4U)) == 0 ||
                result.Failed || result.Bytes.empty())
                throw std::runtime_error("Player icon PNG encoding failed.");
            return std::move(result.Bytes);
        }

        [[nodiscard]] std::vector<std::byte> EncodeWindowsIcon(const BrandingImage& source)
        {
            constexpr std::array sizes{16U, 32U, 48U, 256U};
            std::array<std::vector<std::byte>, sizes.size()> images;
            for (std::size_t index = 0; index < sizes.size(); ++index)
                images[index] = EncodePng(ResizePlayerIcon(source, sizes[index]));
            std::vector<std::byte> result;
            AppendLittleEndian<std::uint16_t>(result, 0);
            AppendLittleEndian<std::uint16_t>(result, 1);
            AppendLittleEndian<std::uint16_t>(result, static_cast<std::uint16_t>(sizes.size()));
            std::uint32_t offset = 6U + static_cast<std::uint32_t>(sizes.size()) * 16U;
            for (std::size_t index = 0; index < sizes.size(); ++index)
            {
                result.push_back(static_cast<std::byte>(sizes[index] == 256U ? 0U : sizes[index]));
                result.push_back(static_cast<std::byte>(sizes[index] == 256U ? 0U : sizes[index]));
                result.push_back(std::byte{0});
                result.push_back(std::byte{0});
                AppendLittleEndian<std::uint16_t>(result, 1);
                AppendLittleEndian<std::uint16_t>(result, 32);
                AppendLittleEndian(result, static_cast<std::uint32_t>(images[index].size()));
                AppendLittleEndian(result, offset);
                offset += static_cast<std::uint32_t>(images[index].size());
            }
            for (const auto& image : images)
                result.insert(result.end(), image.begin(), image.end());
            return result;
        }

#if defined(_WIN32)
        [[nodiscard]] std::uint16_t ReadLittleEndian16(const std::span<const std::byte> bytes, const std::size_t offset)
        {
            return static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[offset])) |
                   static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[offset + 1])) << 8U;
        }

        [[nodiscard]] std::uint32_t ReadLittleEndian32(const std::span<const std::byte> bytes, const std::size_t offset)
        {
            return static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset])) |
                   static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 1])) << 8U |
                   static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 2])) << 16U |
                   static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 3])) << 24U;
        }

        void EmbedWindowsExecutableIcon(const std::filesystem::path& executable, const std::span<const std::byte> icon)
        {
            if (icon.size() < 22 || ReadLittleEndian16(icon, 0) != 0 || ReadLittleEndian16(icon, 2) != 1)
                throw std::runtime_error("Generated Windows player icon is invalid.");
            const auto count = ReadLittleEndian16(icon, 4);
            if (count == 0 || count > 64 || icon.size() < 6U + static_cast<std::size_t>(count) * 16U)
                throw std::runtime_error("Generated Windows player icon directory is invalid.");

            std::vector<std::byte> group;
            group.reserve(6U + static_cast<std::size_t>(count) * 14U);
            AppendLittleEndian<std::uint16_t>(group, 0);
            AppendLittleEndian<std::uint16_t>(group, 1);
            AppendLittleEndian(group, count);
            struct IconResource final
            {
                std::uint16_t Id = 0;
                std::span<const std::byte> Bytes;
            };
            std::vector<IconResource> resources;
            resources.reserve(count);
            for (std::uint16_t index = 0; index < count; ++index)
            {
                const auto entry = 6U + static_cast<std::size_t>(index) * 16U;
                const auto size = ReadLittleEndian32(icon, entry + 8U);
                const auto offset = ReadLittleEndian32(icon, entry + 12U);
                if (size == 0 || offset > icon.size() || size > icon.size() - offset)
                    throw std::runtime_error("Generated Windows player icon entry is invalid.");
                group.insert(group.end(), icon.begin() + static_cast<std::ptrdiff_t>(entry),
                             icon.begin() + static_cast<std::ptrdiff_t>(entry + 12U));
                const auto id = static_cast<std::uint16_t>(index + 1U);
                AppendLittleEndian(group, id);
                resources.push_back({.Id = id, .Bytes = icon.subspan(offset, size)});
            }

            HANDLE update = BeginUpdateResourceW(executable.c_str(), FALSE);
            if (!update)
                throw std::runtime_error("Windows could not open the player executable resources for branding (" +
                                         std::to_string(GetLastError()) + ").");
            try
            {
                constexpr WORD language = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL);
                for (const auto& resource : resources)
                {
                    if (!UpdateResourceW(update, MAKEINTRESOURCEW(3), MAKEINTRESOURCEW(resource.Id), language,
                                         const_cast<std::byte*>(resource.Bytes.data()),
                                         static_cast<DWORD>(resource.Bytes.size())))
                        throw std::runtime_error("Windows could not update a player icon resource (" +
                                                 std::to_string(GetLastError()) + ").");
                }
                if (!UpdateResourceW(update, MAKEINTRESOURCEW(14), MAKEINTRESOURCEW(101), language, group.data(),
                                     static_cast<DWORD>(group.size())))
                    throw std::runtime_error("Windows could not update the player icon group (" +
                                             std::to_string(GetLastError()) + ").");
                if (!EndUpdateResourceW(update, FALSE))
                {
                    update = nullptr;
                    throw std::runtime_error("Windows could not publish the player icon resources (" +
                                             std::to_string(GetLastError()) + ").");
                }
                update = nullptr;
            }
            catch (...)
            {
                if (update)
                    (void)EndUpdateResourceW(update, TRUE);
                throw;
            }
        }
#endif

        [[nodiscard]] std::vector<std::byte> EncodeMacOSIcon(const BrandingImage& source)
        {
            constexpr std::array sizes{128U, 256U, 512U, 1024U};
            constexpr std::array types{std::array{'i', 'c', '0', '7'}, std::array{'i', 'c', '0', '8'},
                                       std::array{'i', 'c', '0', '9'}, std::array{'i', 'c', '1', '0'}};
            std::array<std::vector<std::byte>, sizes.size()> images;
            std::uint32_t total = 8U;
            for (std::size_t index = 0; index < sizes.size(); ++index)
            {
                images[index] = EncodePng(ResizePlayerIcon(source, sizes[index]));
                total += 8U + static_cast<std::uint32_t>(images[index].size());
            }
            std::vector<std::byte> result;
            result.insert(result.end(), {std::byte{'i'}, std::byte{'c'}, std::byte{'n'}, std::byte{'s'}});
            AppendBigEndian(result, total);
            for (std::size_t index = 0; index < sizes.size(); ++index)
            {
                for (const auto character : types[index])
                    result.push_back(static_cast<std::byte>(character));
                AppendBigEndian(result, 8U + static_cast<std::uint32_t>(images[index].size()));
                result.insert(result.end(), images[index].begin(), images[index].end());
            }
            return result;
        }

        [[nodiscard]] bool IsConfinedRelativePath(const std::filesystem::path& path)
        {
            if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory())
                return false;
            const auto normalized = path.lexically_normal();
            return normalized != "." && !normalized.empty() && *normalized.begin() != "..";
        }

        [[nodiscard]] bool IsSameOrChild(const std::filesystem::path& path, const std::filesystem::path& parent)
        {
            const auto normalizedPath = path.lexically_normal();
            const auto normalizedParent = parent.lexically_normal();
            auto pathIterator = normalizedPath.begin();
            for (auto parentIterator = normalizedParent.begin(); parentIterator != normalizedParent.end();
                 ++parentIterator, ++pathIterator)
            {
                if (pathIterator == normalizedPath.end() || *pathIterator != *parentIterator)
                    return false;
            }
            return true;
        }

        [[nodiscard]] bool IsSymbolPath(const std::filesystem::path& relative,
                                        const std::vector<std::filesystem::path>& symbols)
        {
            return std::ranges::any_of(symbols, [&](const auto& symbol) { return IsSameOrChild(relative, symbol); });
        }

        void CopyTemplateTree(const std::filesystem::path& source, const std::filesystem::path& destination,
                              const std::vector<std::filesystem::path>& symbols, const bool includeSymbols)
        {
            if (!std::filesystem::is_directory(source) || std::filesystem::exists(destination))
                throw std::invalid_argument("Player template copy requires an existing source and new destination.");
            std::filesystem::create_directories(destination);
            std::uintmax_t totalBytes = 0;
            std::size_t fileCount = 0;
            std::error_code error;
            for (std::filesystem::recursive_directory_iterator iterator(source, error), end; iterator != end;
                 iterator.increment(error))
            {
                if (error)
                    throw std::filesystem::filesystem_error("Could not enumerate player template.", source, error);
                const auto& entry = *iterator;
                if (entry.is_symlink(error) || error)
                    throw std::runtime_error("Player templates may not contain symbolic links.");
                const auto relative = entry.path().lexically_relative(source);
                if (!IsConfinedRelativePath(relative))
                    throw std::runtime_error("Player template entry escaped its root.");
                if (!includeSymbols && IsSymbolPath(relative, symbols))
                {
                    if (entry.is_directory(error))
                        iterator.disable_recursion_pending();
                    continue;
                }
                const auto output = destination / relative;
                if (entry.is_directory(error))
                {
                    std::filesystem::create_directories(output);
                    continue;
                }
                if (!entry.is_regular_file(error) || error)
                    throw std::runtime_error("Player templates may contain only directories and regular files.");
                const auto bytes = entry.file_size(error);
                if (error || ++fileCount > MaximumPlayerFiles || bytes > MaximumPlayerBytes - totalBytes)
                    throw std::runtime_error("Player template exceeds supported file or size limits.");
                totalBytes += bytes;
                std::filesystem::create_directories(output.parent_path());
                std::filesystem::copy_file(entry.path(), output, std::filesystem::copy_options::none, error);
                if (error)
                    throw std::filesystem::filesystem_error("Could not copy player template file.", entry.path(),
                                                            output, error);
                const auto permissions = entry.status(error).permissions();
                if (error)
                    throw std::filesystem::filesystem_error("Could not inspect player template permissions.",
                                                            entry.path(), error);
                std::filesystem::permissions(output, permissions, std::filesystem::perm_options::replace, error);
                if (error)
                    throw std::filesystem::filesystem_error("Could not apply player template permissions.", output,
                                                            error);
            }
        }

        [[nodiscard]] std::string XmlEscape(const std::string_view value)
        {
            std::string result;
            result.reserve(value.size());
            for (const char character : value)
            {
                switch (character)
                {
                case '&':
                    result += "&amp;";
                    break;
                case '<':
                    result += "&lt;";
                    break;
                case '>':
                    result += "&gt;";
                    break;
                case '\"':
                    result += "&quot;";
                    break;
                case '\'':
                    result += "&apos;";
                    break;
                default:
                    result += character;
                    break;
                }
            }
            return result;
        }

        void WriteMacOSPropertyList(const std::filesystem::path& path, const PlayerSettings& settings)
        {
            const auto product = XmlEscape(settings.ProductName);
            const auto identifier = XmlEscape(settings.ApplicationIdentifier);
            const auto version = XmlEscape(settings.Version);
            const std::string contents = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                                         "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
                                         "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
                                         "<plist version=\"1.0\">\n<dict>\n"
                                         "    <key>CFBundleDisplayName</key><string>" +
                                         product +
                                         "</string>\n"
                                         "    <key>CFBundleExecutable</key><string>" +
                                         product +
                                         "</string>\n"
                                         "    <key>CFBundleIdentifier</key><string>" +
                                         identifier +
                                         "</string>\n"
                                         "    <key>CFBundleName</key><string>" +
                                         product +
                                         "</string>\n"
                                         "    <key>CFBundleIconFile</key><string>PlayerIcon</string>\n"
                                         "    <key>CFBundlePackageType</key><string>APPL</string>\n"
                                         "    <key>CFBundleShortVersionString</key><string>" +
                                         version +
                                         "</string>\n"
                                         "    <key>CFBundleVersion</key><string>" +
                                         version +
                                         "</string>\n"
                                         "    <key>NSHighResolutionCapable</key><true/>\n"
                                         "</dict>\n</plist>\n";
            WriteTextFileAtomically(path, contents);
        }

        void WriteLinuxDesktopEntry(const std::filesystem::path& path, const PlayerSettings& settings)
        {
            WriteTextFileAtomically(
                path, "[Desktop Entry]\nType=Application\nName=" + settings.ProductName + "\nExec=./" +
                          settings.ProductName + "\nIcon=" + settings.ApplicationIdentifier +
                          "\nTerminal=false\nCategories=Game;\nStartupWMClass=" + settings.ApplicationIdentifier +
                          "\n");
        }

        void WriteBrandingSlot(const std::filesystem::path& path, const std::uint64_t offset,
                               const std::uint64_t capacity, const std::span<const std::byte> payload)
        {
            if (capacity < sizeof(std::uint32_t) || payload.size() > capacity - sizeof(std::uint32_t) ||
                offset > MaximumPlayerBytes || capacity > MaximumPlayerBytes - offset)
                throw std::runtime_error("Player template branding slot capacity is invalid.");
            std::fstream stream(path, std::ios::binary | std::ios::in | std::ios::out);
            if (!stream)
                throw std::runtime_error("Player template branding slot file is missing.");
            stream.seekg(0, std::ios::end);
            const auto fileSize = static_cast<std::uint64_t>(stream.tellg());
            if (offset > fileSize || capacity > fileSize - offset)
                throw std::runtime_error("Player template branding slot exceeds its file.");
            stream.seekp(static_cast<std::streamoff>(offset));
            const auto length = static_cast<std::uint32_t>(payload.size());
            std::array<std::byte, sizeof(length)> encodedLength{};
            for (std::size_t index = 0; index < encodedLength.size(); ++index)
                encodedLength[index] = static_cast<std::byte>((length >> (index * 8U)) & 0xffU);
            stream.write(reinterpret_cast<const char*>(encodedLength.data()), encodedLength.size());
            stream.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
            std::vector<char> zeros(static_cast<std::size_t>(capacity - sizeof(length) - payload.size()), '\0');
            stream.write(zeros.data(), static_cast<std::streamsize>(zeros.size()));
            if (!stream)
                throw std::runtime_error("Player template branding slot could not be written.");
        }

        void ConfigureWindowsGuiSubsystem(const std::filesystem::path& path)
        {
            std::fstream stream(path, std::ios::binary | std::ios::in | std::ios::out);
            if (!stream)
                throw std::runtime_error("Windows player template executable is missing.");
            stream.seekg(0, std::ios::end);
            const auto fileSize = static_cast<std::streamoff>(stream.tellg());
            if (fileSize < 0x100)
                throw std::runtime_error("Windows player template has an invalid PE header.");

            const auto readAt = [&](const std::streamoff offset, std::span<std::byte> output)
            {
                stream.seekg(offset);
                stream.read(reinterpret_cast<char*>(output.data()), static_cast<std::streamsize>(output.size()));
                if (!stream)
                    throw std::runtime_error("Windows player template has a truncated PE header.");
            };
            const auto little16 = [](const std::span<const std::byte> bytes)
            {
                return static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[0])) |
                       static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[1]) << 8U);
            };
            const auto little32 = [](const std::span<const std::byte> bytes)
            {
                return static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[0])) |
                       (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[1])) << 8U) |
                       (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[2])) << 16U) |
                       (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[3])) << 24U);
            };

            std::array<std::byte, 64> dos{};
            readAt(0, dos);
            if (dos[0] != std::byte{'M'} || dos[1] != std::byte{'Z'})
                throw std::runtime_error("Windows player template has an invalid DOS header.");
            const auto peOffset = static_cast<std::streamoff>(little32(std::span(dos).subspan(0x3c, 4)));
            if (peOffset < 64 || peOffset > fileSize - 94)
                throw std::runtime_error("Windows player template has an invalid PE offset.");

            std::array<std::byte, 24> header{};
            readAt(peOffset, header);
            if (header[0] != std::byte{'P'} || header[1] != std::byte{'E'} || header[2] != std::byte{0} ||
                header[3] != std::byte{0})
                throw std::runtime_error("Windows player template has an invalid PE signature.");
            const auto optionalBytes = little16(std::span(header).subspan(20, 2));
            if (optionalBytes < 70)
                throw std::runtime_error("Windows player template has an invalid optional header.");
            const auto optionalOffset = peOffset + static_cast<std::streamoff>(header.size());
            std::array<std::byte, 70> optional{};
            readAt(optionalOffset, optional);
            const auto magic = little16(std::span(optional).first(2));
            if (magic != 0x10bU && magic != 0x20bU)
                throw std::runtime_error("Windows player template uses an unsupported PE format.");
            const auto subsystem = little16(std::span(optional).subspan(68, 2));
            if (subsystem != 2U && subsystem != 3U)
                throw std::runtime_error("Windows player template uses an unsupported PE subsystem.");

            constexpr std::array guiSubsystem{std::byte{2}, std::byte{0}};
            stream.seekp(optionalOffset + 68);
            stream.write(reinterpret_cast<const char*>(guiSubsystem.data()), guiSubsystem.size());
            if (!stream)
                throw std::runtime_error("Windows player template subsystem could not be updated.");
        }

        void ApplyWindowsBrandingSlots(const ResolvedPlayerSupport& support, const std::filesystem::path& stagingRoot,
                                       const PlayerSettings& settings, const std::span<const std::byte> icon)
        {
            const auto version = Json{{"productName", settings.ProductName},
                                      {"version", settings.Version},
                                      {"applicationIdentifier", settings.ApplicationIdentifier}}
                                     .dump();
            const auto versionBytes = std::as_bytes(std::span(version.data(), version.size()));
            for (const auto& slot : support.Manifest.BrandingSlots)
            {
                auto relative = slot.Path;
                if (support.Variant.Root != ".")
                    relative = slot.Path.lexically_relative(support.Variant.Root);
                if (!IsConfinedRelativePath(relative))
                    continue;
                if (slot.Kind == "windows-icon")
                    WriteBrandingSlot(stagingRoot / relative, slot.Offset, slot.Size, icon);
                else if (slot.Kind == "windows-version")
                    WriteBrandingSlot(stagingRoot / relative, slot.Offset, slot.Size, versionBytes);
#if defined(_WIN32)
                else if (slot.Kind == "windows-resource-update")
                    EmbedWindowsExecutableIcon(stagingRoot / relative, icon);
#endif
            }
        }

        void WritePlayerDescriptor(const std::filesystem::path& path, const PlayerSettings& settings,
                                   const PlayerBuildProfile& profile)
        {
            const Json document{{"schemaVersion", PlayerBuildDescriptorSchemaVersion},
                                {"playerAbi", PlayerBuildAbiVersion},
                                {"productName", settings.ProductName},
                                {"version", settings.Version},
                                {"applicationIdentifier", settings.ApplicationIdentifier},
                                {"windowTitle", settings.WindowTitle},
                                {"platform", ToString(profile.Platform)},
                                {"architecture", ToString(profile.Architecture)},
                                {"configuration", ToString(profile.Configuration)},
                                {"contentPath", "Content"},
                                {"managedRuntimePath", "Managed"}};
            WriteTextFileAtomically(path, document.dump(2) + '\n');
        }

        [[nodiscard]] std::map<std::string, std::string> SnapshotFiles(const std::filesystem::path& root)
        {
            std::map<std::string, std::string> result;
            std::error_code error;
            for (std::filesystem::recursive_directory_iterator iterator(root, error), end; iterator != end;
                 iterator.increment(error))
            {
                if (error)
                    throw std::filesystem::filesystem_error("Could not inspect staged player.", root, error);
                if (iterator->is_symlink(error) || error)
                    throw std::runtime_error("Staged player may not contain symbolic links.");
                if (!iterator->is_regular_file(error))
                    continue;
                const auto relative = iterator->path().lexically_relative(root);
                if (!IsConfinedRelativePath(relative) || result.size() >= MaximumPlayerFiles)
                    throw std::runtime_error("Staged player file inventory is invalid.");
                result.emplace(PathToUtf8(relative), DigestToString(Sha256File(iterator->path())));
            }
            return result;
        }

        [[nodiscard]] std::optional<std::filesystem::path> FindDescriptor(const std::filesystem::path& executable)
        {
            const auto directory = std::filesystem::absolute(executable).lexically_normal().parent_path();
            const std::array candidates{directory / "PlayerBuild.json",
                                        directory.parent_path() / "Resources" / "PlayerBuild.json"};
            const auto found = std::ranges::find_if(candidates, [](const auto& path)
                                                    { return std::filesystem::is_regular_file(path); });
            return found == candidates.end() ? std::nullopt : std::optional<std::filesystem::path>(*found);
        }

        [[nodiscard]] bool HasEnvironmentVariable(const std::string& name) noexcept
        {
#if defined(_WIN32)
            std::size_t required = 0;
            return getenv_s(&required, nullptr, 0, name.c_str()) == 0 && required > 0;
#else
            return std::getenv(name.c_str()) != nullptr;
#endif
        }

        template <typename Enum>
        [[nodiscard]] Enum ParseEnum(const std::string& value,
                                     const std::initializer_list<std::pair<std::string_view, Enum>> values,
                                     const std::string_view name)
        {
            const auto found =
                std::ranges::find_if(values, [&](const auto& candidate) { return candidate.first == value; });
            if (found == values.end())
                throw std::runtime_error("Unknown " + std::string(name) + ": " + value);
            return found->second;
        }
    } // namespace

    PlayerPackageLayout AssemblePlayerPackage(const ResolvedPlayerSupport& support, const PlayerSettings& settings,
                                              const PlayerBuildProfile& profile,
                                              const std::filesystem::path& stagingRoot,
                                              const std::span<const std::byte> iconSource)
    {
        ValidatePlayerSettings(settings);
        PlayerBuildProfiles validation{.ActiveProfile = profile.Id, .Profiles = {profile}};
        ValidatePlayerBuildProfiles(validation);
        if (support.Manifest.Platform != profile.Platform || support.Manifest.Architecture != profile.Architecture ||
            support.Variant.Configuration != profile.Configuration)
            throw std::invalid_argument("Player support target does not match the build profile.");

        const auto source = support.InstallationRoot / support.Variant.Root;
        CopyTemplateTree(source, stagingRoot, support.Variant.Symbols, profile.IncludeSymbols);
        const auto icon = DecodePlayerIcon(iconSource);
        const auto windowsIcon =
            profile.Platform == PlayerPlatform::Windows ? EncodeWindowsIcon(icon) : std::vector<std::byte>{};
        if (profile.Platform == PlayerPlatform::Windows)
        {
            ApplyWindowsBrandingSlots(support, stagingRoot, settings, windowsIcon);
            ConfigureWindowsGuiSubsystem(stagingRoot / support.Variant.Executable);
        }
        auto sourceExecutable = stagingRoot / support.Variant.Executable;
        if (!std::filesystem::is_regular_file(sourceExecutable))
            throw std::runtime_error("Player support template executable is missing.");

        if (profile.IncludeSymbols && !support.Variant.Symbols.empty())
        {
            const auto symbolRoot = stagingRoot / "Symbols";
            std::filesystem::create_directories(symbolRoot);
            for (const auto& relative : support.Variant.Symbols)
            {
                const auto sourceSymbol = stagingRoot / relative;
                if (!std::filesystem::exists(sourceSymbol))
                    continue;
                const auto destination = symbolRoot / relative.filename();
                RenamePathWithRetry(sourceSymbol, destination);
            }
        }

        PlayerPackageLayout result{.Root = stagingRoot};
        if (profile.Platform == PlayerPlatform::MacOS)
        {
            if (support.Variant.Bundle.empty())
                throw std::runtime_error("macOS player support requires an application bundle.");
            const auto originalBundle = stagingRoot / support.Variant.Bundle;
            const auto destinationBundle = stagingRoot / PathFromUtf8(settings.ProductName + ".app");
            const auto executableWithinBundle = sourceExecutable.lexically_relative(originalBundle);
            if (!IsConfinedRelativePath(executableWithinBundle))
                throw std::runtime_error("macOS player executable is outside its application bundle.");
            const auto renamedExecutable = originalBundle / "Contents" / "MacOS" / PathFromUtf8(settings.ProductName);
            std::filesystem::create_directories(renamedExecutable.parent_path());
            RenamePathWithRetry(sourceExecutable, renamedExecutable);
            WriteMacOSPropertyList(originalBundle / "Contents" / "Info.plist", settings);
            WriteFileAtomically(originalBundle / "Contents" / "Resources" / "PlayerIcon.icns", EncodeMacOSIcon(icon));
            RenamePathWithRetry(originalBundle, destinationBundle);
            result.Executable = destinationBundle / "Contents" / "MacOS" / PathFromUtf8(settings.ProductName);
            result.Descriptor = destinationBundle / "Contents" / "Resources" / "PlayerBuild.json";
            result.Content = destinationBundle / "Contents" / "Resources" / "Content";
            result.ManagedRuntime = destinationBundle / "Contents" / "Resources" / "Managed";
        }
        else
        {
            const auto executableName =
                settings.ProductName +
                (profile.Platform == PlayerPlatform::Windows ? std::string(".exe") : std::string{});
            result.Executable = stagingRoot / PathFromUtf8(executableName);
            if (sourceExecutable != result.Executable)
                RenamePathWithRetry(sourceExecutable, result.Executable);
            result.Descriptor = stagingRoot / "PlayerBuild.json";
            result.Content = stagingRoot / "Content";
            result.ManagedRuntime = stagingRoot / "Managed";
            if (profile.Platform == PlayerPlatform::Linux)
            {
                WriteLinuxDesktopEntry(stagingRoot / PathFromUtf8(settings.ProductName + ".desktop"), settings);
                const auto iconRoot = stagingRoot / "share" / "icons" / "hicolor";
                WriteFileAtomically(iconRoot / "256x256" / "apps" /
                                        PathFromUtf8(settings.ApplicationIdentifier + ".png"),
                                    EncodePng(ResizePlayerIcon(icon, 256)));
                WriteFileAtomically(iconRoot / "512x512" / "apps" /
                                        PathFromUtf8(settings.ApplicationIdentifier + ".png"),
                                    EncodePng(ResizePlayerIcon(icon, 512)));
            }
            else
                WriteFileAtomically(stagingRoot / PathFromUtf8(settings.ProductName + ".ico"), windowsIcon);
        }
        std::filesystem::create_directories(result.Descriptor.parent_path());
        std::filesystem::create_directories(result.Content);
        WritePlayerDescriptor(result.Descriptor, settings, profile);
        return result;
    }

    void RunPlayerSigningHook(const std::filesystem::path& projectRoot, const PlayerSettings& settings,
                              const PlayerBuildProfile& profile, const PlayerPackageLayout& layout)
    {
        const auto& signing = profile.Signing;
        if (signing.Policy == PlayerSigningPolicy::Disabled ||
            (signing.Policy == PlayerSigningPolicy::SignIfConfigured && signing.Command.empty()))
            return;
        if (signing.Command.empty())
            throw std::runtime_error("Player signing is required but no signing command is configured.");
        for (const auto& name : signing.RequiredEnvironment)
            if (!HasEnvironmentVariable(name))
                throw std::runtime_error("Player signing requires environment variable " + name + ".");

        const auto before = SnapshotFiles(layout.Root);
        const auto operation = projectRoot / "Library" / "PlayerBuildHooks" / AssetId::Generate().ToString();
        std::filesystem::create_directories(operation);
        const auto requestPath = operation / "request.json";
        const auto responsePath = operation / "response.json";
        Json files = Json::array();
        for (const auto& [path, digest] : before)
            files.push_back({{"path", path}, {"sha256", digest}});
        const Json request{{"schemaVersion", 1},
                           {"stagingRoot", PathToUtf8(layout.Root)},
                           {"mainArtifact", PathToUtf8(layout.Executable.lexically_relative(layout.Root))},
                           {"platform", ToString(profile.Platform)},
                           {"architecture", ToString(profile.Architecture)},
                           {"configuration", ToString(profile.Configuration)},
                           {"productName", settings.ProductName},
                           {"applicationIdentifier", settings.ApplicationIdentifier},
                           {"version", settings.Version},
                           {"files", std::move(files)}};
        WriteTextFileAtomically(requestPath, request.dump(2) + '\n');

        auto command = signing.Command;
        if (command.is_relative() && std::filesystem::exists(projectRoot / command))
            command = projectRoot / command;
        auto arguments = signing.Arguments;
        arguments.insert(arguments.end(),
                         {"--request", PathToUtf8(requestPath), "--response", PathToUtf8(responsePath)});
        const auto process = RunProcess(command, arguments, projectRoot, std::chrono::seconds(signing.TimeoutSeconds));
        try
        {
            if (process.TimedOut)
                throw std::runtime_error("Player signing hook timed out.");
            if (process.ExitCode != 0 || !std::filesystem::is_regular_file(responsePath))
                throw std::runtime_error("Player signing hook failed with exit code " +
                                         std::to_string(process.ExitCode) + ": " + process.Output);
            const auto response = Json::parse(ReadTextFile(responsePath, MaximumDescriptorBytes));
            if (response.value("schemaVersion", 0U) != 1U || !response.value("success", false) ||
                !response.value("modifiedFiles", Json::array()).is_array())
                throw std::runtime_error("Player signing hook returned a malformed or unsuccessful response.");
            std::set<std::string> declared;
            for (const auto& path : response.at("modifiedFiles"))
            {
                const auto relative = PathFromUtf8(path.get<std::string>());
                if (!IsConfinedRelativePath(relative))
                    throw std::runtime_error("Player signing hook declared an unsafe modified path.");
                declared.emplace(PathToUtf8(relative.lexically_normal()));
            }
            const auto after = SnapshotFiles(layout.Root);
            std::set<std::string> changed;
            for (const auto& [path, digest] : before)
            {
                const auto found = after.find(path);
                if (found == after.end() || found->second != digest)
                    changed.emplace(path);
            }
            for (const auto& [path, digest] : after)
                if (!before.contains(path))
                    changed.emplace(path);
            if (changed != declared)
                throw std::runtime_error("Player signing hook modified files outside its declared response.");
        }
        catch (...)
        {
            std::error_code ignored;
            std::filesystem::remove_all(operation, ignored);
            throw;
        }
        std::error_code ignored;
        std::filesystem::remove_all(operation, ignored);
    }

    void PublishPlayerPackage(const std::filesystem::path& stagingRoot, const std::filesystem::path& destination)
    {
        const auto staging = std::filesystem::absolute(stagingRoot).lexically_normal();
        const auto output = std::filesystem::absolute(destination).lexically_normal();
        if (!std::filesystem::is_directory(staging) || staging == output || staging.root_name() != output.root_name())
            throw std::invalid_argument("Player publication requires a staged directory on the output volume.");
        std::filesystem::create_directories(output.parent_path());
        const auto backup = PathWithSuffix(output, ".previous-" + AssetId::Generate().ToString());
        std::error_code error;
        const bool hadOutput = std::filesystem::exists(output);
        if (hadOutput)
            RenamePathWithRetry(output, backup);
        try
        {
            RenamePathWithRetry(staging, output);
        }
        catch (...)
        {
            if (hadOutput && !std::filesystem::exists(output))
                RenamePathWithRetry(backup, output);
            throw;
        }
        if (hadOutput)
        {
            constexpr std::array delays{std::chrono::milliseconds(10),  std::chrono::milliseconds(20),
                                        std::chrono::milliseconds(40),  std::chrono::milliseconds(80),
                                        std::chrono::milliseconds(160), std::chrono::milliseconds(320)};
            for (const auto delay : delays)
            {
                error.clear();
                std::filesystem::remove_all(backup, error);
                if (!error || !std::filesystem::exists(backup))
                    break;
                std::this_thread::sleep_for(delay);
            }
        }
    }

    std::optional<PackagedPlayerConfiguration> LoadPackagedPlayerConfiguration(const std::filesystem::path& executable)
    {
        const auto descriptor = FindDescriptor(executable);
        if (!descriptor)
            return std::nullopt;
        const auto document = Json::parse(ReadTextFile(*descriptor, MaximumDescriptorBytes));
        if (!document.is_object() || document.value("schemaVersion", 0U) != PlayerBuildDescriptorSchemaVersion ||
            document.value("playerAbi", 0U) != PlayerBuildAbiVersion)
            throw std::runtime_error("Packaged player descriptor is incompatible.");
        PackagedPlayerConfiguration result;
        result.Settings.ProductName = document.at("productName").get<std::string>();
        result.Settings.Version = document.at("version").get<std::string>();
        result.Settings.ApplicationIdentifier = document.at("applicationIdentifier").get<std::string>();
        result.Settings.WindowTitle = document.at("windowTitle").get<std::string>();
        ValidatePlayerSettings(result.Settings);
        result.Platform = ParseEnum<PlayerPlatform>(
            document.at("platform").get<std::string>(),
            {{"windows", PlayerPlatform::Windows}, {"linux", PlayerPlatform::Linux}, {"macos", PlayerPlatform::MacOS}},
            "packaged player platform");
        result.Architecture = ParseEnum<PlayerArchitecture>(
            document.at("architecture").get<std::string>(),
            {{"x86_64", PlayerArchitecture::X86_64}, {"arm64", PlayerArchitecture::Arm64}},
            "packaged player architecture");
        result.Configuration =
            ParseEnum<PlayerBuildConfiguration>(document.at("configuration").get<std::string>(),
                                                {{"development", PlayerBuildConfiguration::Development},
                                                 {"release", PlayerBuildConfiguration::Release},
                                                 {"dist", PlayerBuildConfiguration::Dist}},
                                                "packaged player configuration");
        const auto content = PathFromUtf8(document.at("contentPath").get<std::string>());
        const auto managed = PathFromUtf8(document.at("managedRuntimePath").get<std::string>());
        if (!IsConfinedRelativePath(content) || !IsConfinedRelativePath(managed))
            throw std::runtime_error("Packaged player descriptor paths are unsafe.");
        result.Content = descriptor->parent_path() / content;
        result.ManagedRuntime = descriptor->parent_path() / managed;
        if (!std::filesystem::is_regular_file(result.Content / "runtime-manifest.json") ||
            !std::filesystem::is_directory(result.ManagedRuntime))
            throw std::runtime_error("Packaged player content or managed runtime is missing.");
        return result;
    }

    PlayerBuildStatusDocument ReadPlayerBuildStatusDocument(const std::filesystem::path& path)
    {
        const auto document = Json::parse(ReadTextFile(path, MaximumDescriptorBytes));
        if (!document.is_object() || document.value("schemaVersion", 0U) != 1U)
            throw std::runtime_error("Player builder status document is incompatible.");
        PlayerBuildStatusDocument result;
        result.State = document.at("state").get<std::string>();
        if (result.State != "running" && result.State != "succeeded" && result.State != "failed")
            throw std::runtime_error("Player builder status document has an unknown state.");
        result.Phase = document.value("phase", std::string{});
        result.Progress = std::clamp(document.value("progress", 0.0F), 0.0F, 1.0F);
        result.Message = document.value("message", std::string{});
        result.Output = PathFromUtf8(document.value("output", std::string{}));
        result.Executable = PathFromUtf8(document.value("executable", std::string{}));
        return result;
    }
} // namespace Keire::Detail
