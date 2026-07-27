#pragma once

#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/AssetSystem.h"
#include "Keire/Audio/AudioSystem.h"

#include <memory>
#include <span>
#include <vector>

namespace Keire
{
    class KEIRE_API AudioClipAsset final : public Asset
    {
      public:
        explicit AudioClipAsset(std::shared_ptr<const AudioClipData> clip);

        [[nodiscard]] static constexpr AssetTypeId StaticType() noexcept
        {
            return AssetTypeId(AssetId(0x4b45495245415544ULL, 0x494f434c49500001ULL));
        }

        [[nodiscard]] AssetTypeId Type() const noexcept override { return StaticType(); }
        [[nodiscard]] std::size_t ResidentBytes() const noexcept override;
        [[nodiscard]] const std::shared_ptr<const AudioClipData>& Clip() const noexcept { return m_Clip; }
        [[nodiscard]] std::uint64_t FrameCount() const noexcept;
        [[nodiscard]] float DurationSeconds() const noexcept;

        [[nodiscard]] static std::vector<std::byte> Encode(const AudioClipData& clip);
        [[nodiscard]] static Ref<AudioClipAsset> Decode(std::span<const std::byte> bytes);
        [[nodiscard]] static Ref<AudioClipAsset> Silence();

      private:
        std::shared_ptr<const AudioClipData> m_Clip;
    };

    [[nodiscard]] KEIRE_API AssetImporterRegistration CreateAudioClipAssetImporter();
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateAudioClipAssetDecoder();
} // namespace Keire
