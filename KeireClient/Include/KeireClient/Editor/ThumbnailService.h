#pragma once

#include "Keire/Core.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace KeireEditor
{
    struct ThumbnailMeshInstance
    {
        Keire::Ref<const Keire::MeshAsset> Mesh;
        Keire::Matrix4 Transform;
    };

    struct ThumbnailRequest
    {
        Keire::AssetId Asset;
        Keire::AssetTypeId Type;
        Keire::Ref<const Keire::Asset> PreviewAsset;
        Keire::Ref<const Keire::ShaderAsset> PreviewShader;
        Keire::Ref<const Keire::Texture2DAsset> PreviewTexture;
        std::vector<ThumbnailMeshInstance> PreviewMeshes;
        std::filesystem::path RelativePath;
        std::string Digest;
        bool Missing = false;
    };

    struct ThumbnailResult
    {
        Keire::AssetId Asset;
        std::uint32_t Width = 0;
        std::uint32_t Height = 0;
        std::vector<std::byte> Pixels;
    };

    class ThumbnailService final
    {
      public:
        explicit ThumbnailService(std::filesystem::path cacheDirectory, std::size_t queueCapacity = 256,
                                  Keire::Ref<Keire::JobSystem> jobs = {});
        ~ThumbnailService();

        ThumbnailService(const ThumbnailService&) = delete;
        ThumbnailService& operator=(const ThumbnailService&) = delete;

        using Provider = std::function<std::vector<std::byte>(const ThumbnailRequest&, std::uint32_t, std::uint32_t)>;
        void RegisterProvider(std::string extension, std::uint32_t version, Provider provider);
        [[nodiscard]] bool Request(ThumbnailRequest request);
        [[nodiscard]] std::vector<ThumbnailResult> DrainCompleted(std::size_t maximum = 32);
        void CancelAll() noexcept;
        [[nodiscard]] std::size_t PendingCount() const noexcept;

      private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };

    [[nodiscard]] std::vector<std::byte> MakeFolderThumbnail(std::uint32_t width, std::uint32_t height,
                                                             bool missing = false);
} // namespace KeireEditor
