#include <KeireAssetWorkerInternal/FfmpegTextureImportBackend.h>

#if defined(KEIRE_HAS_FFMPEG)
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavutil/pixfmt.h>
}
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

namespace Keire::Detail
{
#if defined(KEIRE_HAS_FFMPEG)
    namespace
    {
        constexpr std::uint32_t MaximumDimension = 16384;

        [[nodiscard]] std::string AvError(const int code)
        {
            std::array<char, AV_ERROR_MAX_STRING_SIZE> text{};
            av_strerror(code, text.data(), text.size());
            return text.data();
        }

        void RequireAv(const int code, const char* operation)
        {
            if (code < 0)
                throw std::runtime_error(std::string(operation) + ": " + AvError(code));
        }

        struct CodecDeleter final
        {
            void operator()(AVCodecContext* context) const noexcept { avcodec_free_context(&context); }
        };

        struct PacketDeleter final
        {
            void operator()(AVPacket* packet) const noexcept { av_packet_free(&packet); }
        };

        struct FrameDeleter final
        {
            void operator()(AVFrame* frame) const noexcept { av_frame_free(&frame); }
        };

        [[nodiscard]] float HalfToFloat(const std::uint16_t value) noexcept
        {
            const auto sign = (value & 0x8000U) != 0 ? -1.0F : 1.0F;
            const auto exponent = static_cast<std::uint16_t>((value >> 10U) & 0x1fU);
            const auto fraction = static_cast<std::uint16_t>(value & 0x03ffU);
            if (exponent == 0)
                return fraction == 0 ? std::copysign(0.0F, sign) : sign * std::ldexp(static_cast<float>(fraction), -24);
            if (exponent == 0x1fU)
                return fraction == 0 ? std::copysign(std::numeric_limits<float>::infinity(), sign)
                                     : std::numeric_limits<float>::quiet_NaN();
            return sign * std::ldexp(1.0F + static_cast<float>(fraction) / 1024.0F, static_cast<int>(exponent) - 15);
        }

        template <typename Type>
        [[nodiscard]] Type ReadChannel(const std::uint8_t* row, const std::size_t index) noexcept
        {
            Type value{};
            std::memcpy(&value, row + index * sizeof(Type), sizeof(Type));
            return value;
        }

        [[nodiscard]] const std::uint8_t* Row(const AVFrame& frame, const std::size_t plane, const int y)
        {
            if (!frame.data[plane])
                throw std::runtime_error("The OpenEXR decoder returned a missing image plane.");
            if (frame.linesize[plane] >= 0)
                return frame.data[plane] + static_cast<std::ptrdiff_t>(y) * frame.linesize[plane];
            return frame.data[plane] + static_cast<std::ptrdiff_t>(frame.height - 1 - y) * -frame.linesize[plane];
        }

        [[nodiscard]] DecodedFloatTexture Convert(const AVFrame& frame)
        {
            if (frame.width <= 0 || frame.height <= 0 || frame.width > static_cast<int>(MaximumDimension) ||
                frame.height > static_cast<int>(MaximumDimension))
                throw std::runtime_error("OpenEXR dimensions are invalid or exceed 16384 pixels.");
            DecodedFloatTexture result;
            result.Width = static_cast<std::uint32_t>(frame.width);
            result.Height = static_cast<std::uint32_t>(frame.height);
            result.Pixels.resize(static_cast<std::size_t>(result.Width) * result.Height * 4U, 1.0F);

            const auto set =
                [&](const std::size_t pixel, const float red, const float green, const float blue, const float alpha)
            {
                result.Pixels[pixel * 4U] = red;
                result.Pixels[pixel * 4U + 1U] = green;
                result.Pixels[pixel * 4U + 2U] = blue;
                result.Pixels[pixel * 4U + 3U] = alpha;
            };
            for (int y = 0; y < frame.height; ++y)
                for (int x = 0; x < frame.width; ++x)
                {
                    const auto pixel = static_cast<std::size_t>(y) * result.Width + static_cast<std::size_t>(x);
                    if (frame.format == AV_PIX_FMT_GBRPF32 || frame.format == AV_PIX_FMT_GBRAPF32)
                    {
                        const float green = ReadChannel<float>(Row(frame, 0, y), x);
                        const float blue = ReadChannel<float>(Row(frame, 1, y), x);
                        const float red = ReadChannel<float>(Row(frame, 2, y), x);
                        const float alpha =
                            frame.format == AV_PIX_FMT_GBRAPF32 ? ReadChannel<float>(Row(frame, 3, y), x) : 1.0F;
                        set(pixel, red, green, blue, alpha);
                    }
                    else if (frame.format == AV_PIX_FMT_GBRPF16 || frame.format == AV_PIX_FMT_GBRAPF16)
                    {
                        const float green = HalfToFloat(ReadChannel<std::uint16_t>(Row(frame, 0, y), x));
                        const float blue = HalfToFloat(ReadChannel<std::uint16_t>(Row(frame, 1, y), x));
                        const float red = HalfToFloat(ReadChannel<std::uint16_t>(Row(frame, 2, y), x));
                        const float alpha = frame.format == AV_PIX_FMT_GBRAPF16
                                                ? HalfToFloat(ReadChannel<std::uint16_t>(Row(frame, 3, y), x))
                                                : 1.0F;
                        set(pixel, red, green, blue, alpha);
                    }
                    else if (frame.format == AV_PIX_FMT_RGBF32 || frame.format == AV_PIX_FMT_RGBAF32)
                    {
                        const auto* row = Row(frame, 0, y);
                        const auto channels = frame.format == AV_PIX_FMT_RGBAF32 ? std::size_t{4} : std::size_t{3};
                        const auto offset = static_cast<std::size_t>(x) * channels;
                        set(pixel, ReadChannel<float>(row, offset), ReadChannel<float>(row, offset + 1U),
                            ReadChannel<float>(row, offset + 2U),
                            channels == 4 ? ReadChannel<float>(row, offset + 3U) : 1.0F);
                    }
                    else if (frame.format == AV_PIX_FMT_RGBAF16)
                    {
                        const auto* row = Row(frame, 0, y);
                        const auto offset = static_cast<std::size_t>(x) * 4U;
                        set(pixel, HalfToFloat(ReadChannel<std::uint16_t>(row, offset)),
                            HalfToFloat(ReadChannel<std::uint16_t>(row, offset + 1U)),
                            HalfToFloat(ReadChannel<std::uint16_t>(row, offset + 2U)),
                            HalfToFloat(ReadChannel<std::uint16_t>(row, offset + 3U)));
                    }
                    else if (frame.format == AV_PIX_FMT_GRAYF32)
                    {
                        const float value = ReadChannel<float>(Row(frame, 0, y), x);
                        set(pixel, value, value, value, 1.0F);
                    }
                    else
                    {
                        throw std::runtime_error("The OpenEXR decoder returned an unsupported float pixel format: " +
                                                 std::to_string(frame.format));
                    }
                }
            return result;
        }

        [[nodiscard]] DecodedFloatTexture DecodeOpenExr(const std::span<const std::byte> source)
        {
            if (source.empty() || source.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
                throw std::invalid_argument("OpenEXR source is empty or exceeds the decoder limit.");
            const auto* definition = avcodec_find_decoder(AV_CODEC_ID_EXR);
            if (!definition)
                throw std::runtime_error("The private FFmpeg build does not contain the OpenEXR decoder.");
            std::unique_ptr<AVCodecContext, CodecDeleter> decoder(avcodec_alloc_context3(definition));
            std::unique_ptr<AVPacket, PacketDeleter> packet(av_packet_alloc());
            std::unique_ptr<AVFrame, FrameDeleter> frame(av_frame_alloc());
            if (!decoder || !packet || !frame)
                throw std::bad_alloc();
            decoder->thread_count = 0;
            RequireAv(avcodec_open2(decoder.get(), definition, nullptr), "Could not open the OpenEXR decoder");
            RequireAv(av_new_packet(packet.get(), static_cast<int>(source.size())),
                      "Could not allocate the OpenEXR decode packet");
            std::memcpy(packet->data, source.data(), source.size());
            RequireAv(avcodec_send_packet(decoder.get(), packet.get()), "Could not submit the OpenEXR image");
            RequireAv(avcodec_receive_frame(decoder.get(), frame.get()), "Could not decode the OpenEXR image");
            return Convert(*frame);
        }
    } // namespace

    TextureDecodeBackend CreateFfmpegTextureImportBackend() { return DecodeOpenExr; }
#else
    TextureDecodeBackend CreateFfmpegTextureImportBackend()
    {
        return [](std::span<const std::byte>) -> DecodedFloatTexture
        { throw std::runtime_error("OpenEXR import requires the configured private FFmpeg dependency."); };
    }
#endif
} // namespace Keire::Detail
