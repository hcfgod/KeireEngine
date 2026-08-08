#include <KeireAssetWorkerInternal/FfmpegAudioImportBackend.h>

#if defined(KEIRE_HAS_FFMPEG)
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}
#endif

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Keire::Detail
{
#if defined(KEIRE_HAS_FFMPEG)
    namespace
    {
        constexpr std::size_t AvioBufferBytes = std::size_t{64} * 1024U;
        constexpr std::size_t MaximumEncodedBytes = std::size_t{768} * 1024U * 1024U;
        constexpr std::size_t MaximumFastPcmBytes = std::size_t{256} * 1024U * 1024U;
        constexpr std::uint64_t MaximumFrames = 384000ULL * 60ULL * 60ULL * 4ULL;

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

        struct InputState
        {
            std::span<const std::byte> Bytes;
            std::size_t Cursor = 0;
        };

        int ReadInput(void* opaque, std::uint8_t* destination, const int requested)
        {
            auto& state = *static_cast<InputState*>(opaque);
            if (state.Cursor >= state.Bytes.size())
                return AVERROR_EOF;
            const auto count =
                std::min<std::size_t>(static_cast<std::size_t>(requested), state.Bytes.size() - state.Cursor);
            std::copy_n(reinterpret_cast<const std::uint8_t*>(state.Bytes.data()) + state.Cursor, count, destination);
            state.Cursor += count;
            return static_cast<int>(count);
        }

        std::int64_t SeekInput(void* opaque, const std::int64_t offset, const int origin)
        {
            auto& state = *static_cast<InputState*>(opaque);
            if ((origin & AVSEEK_SIZE) != 0)
                return static_cast<std::int64_t>(state.Bytes.size());

            std::int64_t base = 0;
            switch (origin & ~AVSEEK_FORCE)
            {
            case SEEK_SET:
                break;
            case SEEK_CUR:
                base = static_cast<std::int64_t>(state.Cursor);
                break;
            case SEEK_END:
                base = static_cast<std::int64_t>(state.Bytes.size());
                break;
            default:
                return AVERROR(EINVAL);
            }
            if ((offset < 0 && base < -offset) ||
                (offset > 0 && base > std::numeric_limits<std::int64_t>::max() - offset))
                return AVERROR(EINVAL);
            const auto target = base + offset;
            if (target < 0 || static_cast<std::uint64_t>(target) > state.Bytes.size())
                return AVERROR(EINVAL);
            state.Cursor = static_cast<std::size_t>(target);
            return target;
        }

        struct OutputState
        {
            std::vector<std::byte> Bytes;
        };

        int WriteOutput(void* opaque, const std::uint8_t* source, const int count)
        {
            auto& state = *static_cast<OutputState*>(opaque);
            if (count < 0 || state.Bytes.size() > MaximumEncodedBytes - static_cast<std::size_t>(count))
                return AVERROR(ENOSPC);
            const auto* begin = reinterpret_cast<const std::byte*>(source);
            state.Bytes.insert(state.Bytes.end(), begin, begin + count);
            return count;
        }

        void AppendWaveU16(std::vector<std::byte>& output, const std::uint16_t value)
        {
            output.push_back(static_cast<std::byte>(value & 0xffU));
            output.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
        }

        void AppendWaveU32(std::vector<std::byte>& output, const std::uint32_t value)
        {
            for (std::uint32_t shift = 0; shift < 32; shift += 8)
                output.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
        }

        void AppendFourCc(std::vector<std::byte>& output, const std::string_view value)
        {
            for (const auto character : value)
                output.push_back(static_cast<std::byte>(character));
        }

        [[nodiscard]] std::vector<std::byte> BuildPcmWave(const std::span<const std::byte> pcm,
                                                          const std::uint32_t sampleRate, const std::uint16_t channels)
        {
            if (pcm.empty() || pcm.size() > std::numeric_limits<std::uint32_t>::max() - 36U)
                throw std::runtime_error("Fast audio conversion exceeded the RIFF/WAV size limit.");
            constexpr std::uint16_t bitsPerSample = 16;
            const auto blockAlign = static_cast<std::uint16_t>(channels * (bitsPerSample / 8U));
            const auto byteRate = sampleRate * blockAlign;
            std::vector<std::byte> output;
            output.reserve(44U + pcm.size());
            AppendFourCc(output, "RIFF");
            AppendWaveU32(output, static_cast<std::uint32_t>(36U + pcm.size()));
            AppendFourCc(output, "WAVE");
            AppendFourCc(output, "fmt ");
            AppendWaveU32(output, 16U);
            AppendWaveU16(output, 1U);
            AppendWaveU16(output, channels);
            AppendWaveU32(output, sampleRate);
            AppendWaveU32(output, byteRate);
            AppendWaveU16(output, blockAlign);
            AppendWaveU16(output, bitsPerSample);
            AppendFourCc(output, "data");
            AppendWaveU32(output, static_cast<std::uint32_t>(pcm.size()));
            output.insert(output.end(), pcm.begin(), pcm.end());
            return output;
        }

        class AvioOwner final
        {
          public:
            AvioOwner(void* opaque, const bool writable, int (*read)(void*, std::uint8_t*, int),
                      int (*write)(void*, const std::uint8_t*, int), std::int64_t (*seek)(void*, std::int64_t, int))
            {
                auto* buffer = static_cast<std::uint8_t*>(av_malloc(AvioBufferBytes));
                if (buffer == nullptr)
                    throw std::bad_alloc();
                m_Context = avio_alloc_context(buffer, static_cast<int>(AvioBufferBytes), writable ? 1 : 0, opaque,
                                               read, write, seek);
                if (m_Context == nullptr)
                {
                    av_free(buffer);
                    throw std::bad_alloc();
                }
            }

            ~AvioOwner()
            {
                if (m_Context != nullptr)
                {
                    av_freep(static_cast<void*>(&m_Context->buffer));
                    avio_context_free(&m_Context);
                }
            }

            AvioOwner(const AvioOwner&) = delete;
            AvioOwner& operator=(const AvioOwner&) = delete;

            [[nodiscard]] AVIOContext* Get() const noexcept { return m_Context; }

          private:
            AVIOContext* m_Context = nullptr;
        };

        struct InputFormatDeleter
        {
            void operator()(AVFormatContext* context) const noexcept
            {
                if (context != nullptr)
                    avformat_close_input(&context);
            }
        };

        struct OutputFormatDeleter
        {
            void operator()(AVFormatContext* context) const noexcept
            {
                if (context != nullptr)
                    avformat_free_context(context);
            }
        };

        struct CodecDeleter
        {
            void operator()(AVCodecContext* context) const noexcept
            {
                if (context != nullptr)
                    avcodec_free_context(&context);
            }
        };

        struct FrameDeleter
        {
            void operator()(AVFrame* frame) const noexcept
            {
                if (frame != nullptr)
                    av_frame_free(&frame);
            }
        };

        struct PacketDeleter
        {
            void operator()(AVPacket* packet) const noexcept
            {
                if (packet != nullptr)
                    av_packet_free(&packet);
            }
        };

        struct SwrDeleter
        {
            void operator()(SwrContext* context) const noexcept
            {
                if (context != nullptr)
                    swr_free(&context);
            }
        };

        struct FifoDeleter
        {
            void operator()(AVAudioFifo* fifo) const noexcept
            {
                if (fifo != nullptr)
                    av_audio_fifo_free(fifo);
            }
        };

        using InputFormat = std::unique_ptr<AVFormatContext, InputFormatDeleter>;
        using OutputFormat = std::unique_ptr<AVFormatContext, OutputFormatDeleter>;
        using Codec = std::unique_ptr<AVCodecContext, CodecDeleter>;
        using Frame = std::unique_ptr<AVFrame, FrameDeleter>;
        using Packet = std::unique_ptr<AVPacket, PacketDeleter>;
        using Resampler = std::unique_ptr<SwrContext, SwrDeleter>;
        using AudioFifo = std::unique_ptr<AVAudioFifo, FifoDeleter>;

        [[nodiscard]] int ResolveAudioStream(const AVFormatContext& format, const std::int64_t selected)
        {
            if (selected < 0)
                throw std::invalid_argument("Audio stream selection cannot be negative.");
            std::int64_t ordinal = 0;
            for (unsigned index = 0; index < format.nb_streams; ++index)
            {
                if (format.streams[index]->codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
                    continue;
                if (ordinal++ == selected)
                    return static_cast<int>(index);
            }
            throw std::invalid_argument("The selected audio stream is not present in this media source.");
        }

        void EncodeAvailable(AVAudioFifo& fifo, AVCodecContext& encoder, AVFormatContext& output,
                             AVStream& outputStream, AVPacket& packet, std::int64_t& timestamp, const bool flush)
        {
            const auto preferred = encoder.frame_size > 0 ? encoder.frame_size : 4096;
            while (av_audio_fifo_size(&fifo) >= preferred || (flush && av_audio_fifo_size(&fifo) > 0))
            {
                const auto samples = std::min(preferred, av_audio_fifo_size(&fifo));
                Frame frame(av_frame_alloc());
                if (!frame)
                    throw std::bad_alloc();
                frame->format = encoder.sample_fmt;
                frame->sample_rate = encoder.sample_rate;
                frame->nb_samples = samples;
                RequireAv(av_channel_layout_copy(&frame->ch_layout, &encoder.ch_layout),
                          "Could not copy the output channel layout");
                RequireAv(av_frame_get_buffer(frame.get(), 0), "Could not allocate an audio encode frame");
                if (av_audio_fifo_read(&fifo, reinterpret_cast<void**>(frame->extended_data), samples) != samples)
                    throw std::runtime_error("Could not drain the audio conversion buffer.");
                frame->pts = timestamp;
                timestamp += samples;
                RequireAv(avcodec_send_frame(&encoder, frame.get()), "Could not submit audio to the FLAC encoder");
                while (true)
                {
                    const auto status = avcodec_receive_packet(&encoder, &packet);
                    if (status == AVERROR(EAGAIN) || status == AVERROR_EOF)
                        break;
                    RequireAv(status, "Could not receive FLAC output");
                    av_packet_rescale_ts(&packet, encoder.time_base, outputStream.time_base);
                    packet.stream_index = outputStream.index;
                    RequireAv(av_interleaved_write_frame(&output, &packet), "Could not write FLAC output");
                    av_packet_unref(&packet);
                }
            }
        }

        void ConvertFrame(const AVFrame& decoded, SwrContext& resampler, AVAudioFifo& fifo,
                          const AVCodecContext& encoder)
        {
            const auto capacity =
                static_cast<int>(av_rescale_rnd(swr_get_delay(&resampler, decoded.sample_rate) + decoded.nb_samples,
                                                encoder.sample_rate, decoded.sample_rate, AV_ROUND_UP));
            Frame converted(av_frame_alloc());
            if (!converted)
                throw std::bad_alloc();
            converted->format = encoder.sample_fmt;
            converted->sample_rate = encoder.sample_rate;
            converted->nb_samples = capacity;
            RequireAv(av_channel_layout_copy(&converted->ch_layout, &encoder.ch_layout),
                      "Could not copy the converted channel layout");
            RequireAv(av_frame_get_buffer(converted.get(), 0), "Could not allocate an audio conversion frame");
            const auto samples =
                swr_convert(&resampler, converted->extended_data, capacity, decoded.extended_data, decoded.nb_samples);
            RequireAv(samples, "Could not resample the source audio");
            RequireAv(av_audio_fifo_realloc(&fifo, av_audio_fifo_size(&fifo) + samples),
                      "Could not grow the audio conversion buffer");
            if (av_audio_fifo_write(&fifo, reinterpret_cast<void**>(converted->extended_data), samples) != samples)
                throw std::runtime_error("Could not buffer converted audio.");
        }

        void AppendPcm(const AVFrame& decoded, SwrContext& resampler, const AVCodecContext& target,
                       std::vector<std::byte>& pcm)
        {
            const auto capacity =
                static_cast<int>(av_rescale_rnd(swr_get_delay(&resampler, decoded.sample_rate) + decoded.nb_samples,
                                                target.sample_rate, decoded.sample_rate, AV_ROUND_UP));
            const auto channels = static_cast<std::size_t>(target.ch_layout.nb_channels);
            if (capacity <= 0 || channels == 0)
                return;
            const auto maximumSamples = MaximumFastPcmBytes / sizeof(std::int16_t);
            if (static_cast<std::size_t>(capacity) > maximumSamples / channels ||
                pcm.size() / sizeof(std::int16_t) > maximumSamples - static_cast<std::size_t>(capacity) * channels)
            {
                throw std::runtime_error(
                    "Fast PCM conversion exceeded 256 MiB. Select the compressed transcode mode for this source.");
            }
            const auto previousSize = pcm.size();
            pcm.resize(previousSize + static_cast<std::size_t>(capacity) * channels * sizeof(std::int16_t));
            auto* destination = reinterpret_cast<std::uint8_t*>(pcm.data() + previousSize);
            std::uint8_t* planes[]{destination};
            const auto samples = swr_convert(&resampler, planes, capacity, decoded.extended_data, decoded.nb_samples);
            RequireAv(samples, "Could not convert source audio to PCM");
            pcm.resize(previousSize + static_cast<std::size_t>(samples) * channels * sizeof(std::int16_t));
        }

        void FlushPcm(SwrContext& resampler, const AVCodecContext& target, std::vector<std::byte>& pcm)
        {
            const auto channels = static_cast<std::size_t>(target.ch_layout.nb_channels);
            while (swr_get_delay(&resampler, target.sample_rate) > 0)
            {
                const auto capacity = static_cast<int>(swr_get_delay(&resampler, target.sample_rate));
                const auto maximumSamples = MaximumFastPcmBytes / sizeof(std::int16_t);
                if (capacity <= 0 || static_cast<std::size_t>(capacity) > maximumSamples / channels ||
                    pcm.size() / sizeof(std::int16_t) > maximumSamples - static_cast<std::size_t>(capacity) * channels)
                {
                    throw std::runtime_error(
                        "Fast PCM conversion exceeded 256 MiB. Select the compressed transcode mode for this source.");
                }
                const auto previousSize = pcm.size();
                pcm.resize(previousSize + static_cast<std::size_t>(capacity) * channels * sizeof(std::int16_t));
                auto* destination = reinterpret_cast<std::uint8_t*>(pcm.data() + previousSize);
                std::uint8_t* planes[]{destination};
                const auto samples = swr_convert(&resampler, planes, capacity, nullptr, 0);
                RequireAv(samples, "Could not flush converted PCM audio");
                pcm.resize(previousSize + static_cast<std::size_t>(samples) * channels * sizeof(std::int16_t));
                if (samples == 0)
                    break;
            }
        }

        [[nodiscard]] AudioTranscodeResult Transcode(const AssetImportContext& context,
                                                     const std::span<const std::byte> source)
        {
            if (source.empty() || source.size() > MaximumEncodedBytes)
                throw std::invalid_argument("Audio source is empty or exceeds the 768 MiB import limit.");

            InputState inputState{source};
            AvioOwner inputIo(&inputState, false, ReadInput, nullptr, SeekInput);
            auto* rawInput = avformat_alloc_context();
            if (rawInput == nullptr)
                throw std::bad_alloc();
            rawInput->pb = inputIo.Get();
            rawInput->flags |= AVFMT_FLAG_CUSTOM_IO | AVFMT_FLAG_FAST_SEEK;
            rawInput->probesize = 8LL * 1024LL * 1024LL;
            rawInput->max_analyze_duration = 10LL * AV_TIME_BASE;
            RequireAv(avformat_open_input(&rawInput, nullptr, nullptr, nullptr), "Could not open the media container");
            InputFormat input(rawInput);
            RequireAv(avformat_find_stream_info(input.get(), nullptr), "Could not inspect the media streams");

            std::int64_t selectedStream = 0;
            if (const auto setting = context.ImportSettings.find("audioStream");
                setting != context.ImportSettings.end())
                selectedStream = std::get<std::int64_t>(setting->second);
            const auto streamIndex = ResolveAudioStream(*input, selectedStream);
            const auto* sourceStream = input->streams[streamIndex];
            const auto* decoderDefinition = avcodec_find_decoder(sourceStream->codecpar->codec_id);
            if (decoderDefinition == nullptr)
                throw std::runtime_error("No decoder is available for the selected audio stream.");
            Codec decoder(avcodec_alloc_context3(decoderDefinition));
            if (!decoder)
                throw std::bad_alloc();
            RequireAv(avcodec_parameters_to_context(decoder.get(), input->streams[streamIndex]->codecpar),
                      "Could not initialize the audio decoder");
            decoder->thread_count = 0;
            RequireAv(avcodec_open2(decoder.get(), decoderDefinition, nullptr), "Could not open the audio decoder");

            bool fastPcm = true;
            if (const auto setting = context.ImportSettings.find("transcodeMode");
                setting != context.ImportSettings.end())
            {
                if (const auto* value = std::get_if<std::string>(&setting->second))
                    fastPcm = *value != "compressed";
            }
            std::int64_t estimatedFrames = 0;
            if (sourceStream->duration > 0)
                estimatedFrames =
                    av_rescale_q(sourceStream->duration, sourceStream->time_base, AVRational{1, decoder->sample_rate});
            else if (input->duration > 0)
                estimatedFrames = av_rescale_q(input->duration, AV_TIME_BASE_Q, AVRational{1, decoder->sample_rate});
            const auto outputChannels = static_cast<std::size_t>(decoder->ch_layout.nb_channels);
            if (estimatedFrames <= 0 || outputChannels == 0 ||
                static_cast<std::uint64_t>(estimatedFrames) >
                    MaximumFastPcmBytes / (outputChannels * sizeof(std::int16_t)))
            {
                fastPcm = false;
            }

            const auto* encoderDefinition = avcodec_find_encoder(AV_CODEC_ID_FLAC);
            if (encoderDefinition == nullptr)
                throw std::runtime_error("The private FFmpeg build does not contain the FLAC encoder.");
            Codec encoder(avcodec_alloc_context3(encoderDefinition));
            if (!encoder)
                throw std::bad_alloc();
            encoder->sample_rate = decoder->sample_rate;
            encoder->sample_fmt = AV_SAMPLE_FMT_S16;
            encoder->time_base = AVRational{1, encoder->sample_rate};
            if (decoder->ch_layout.nb_channels == 0)
                av_channel_layout_default(&decoder->ch_layout, decoder->ch_layout.nb_channels);
            RequireAv(av_channel_layout_copy(&encoder->ch_layout, &decoder->ch_layout),
                      "Could not configure the FLAC channel layout");
            av_opt_set_int(encoder->priv_data, "compression_level", 0, 0);
            RequireAv(avcodec_open2(encoder.get(), encoderDefinition, nullptr), "Could not open the FLAC encoder");

            OutputFormat output;
            AVStream* outputStream = nullptr;
            OutputState outputState;
            std::unique_ptr<AvioOwner> outputIo;
            if (!fastPcm)
            {
                auto* rawOutput = static_cast<AVFormatContext*>(nullptr);
                RequireAv(avformat_alloc_output_context2(&rawOutput, nullptr, "flac", nullptr),
                          "Could not initialize the FLAC container");
                output.reset(rawOutput);
                outputStream = avformat_new_stream(output.get(), nullptr);
                if (outputStream == nullptr)
                    throw std::bad_alloc();
                outputStream->time_base = encoder->time_base;
                RequireAv(avcodec_parameters_from_context(outputStream->codecpar, encoder.get()),
                          "Could not describe the FLAC stream");
                outputState.Bytes.reserve(std::min(source.size(), std::size_t{16} * 1024U * 1024U));
                outputIo = std::make_unique<AvioOwner>(&outputState, true, nullptr, WriteOutput, nullptr);
                output->pb = outputIo->Get();
                output->flags |= AVFMT_FLAG_CUSTOM_IO;
                RequireAv(avformat_write_header(output.get(), nullptr), "Could not write the FLAC header");
            }

            SwrContext* rawResampler = nullptr;
            RequireAv(swr_alloc_set_opts2(&rawResampler, &encoder->ch_layout, encoder->sample_fmt, encoder->sample_rate,
                                          &decoder->ch_layout, decoder->sample_fmt, decoder->sample_rate, 0, nullptr),
                      "Could not create the audio resampler");
            Resampler resampler(rawResampler);
            RequireAv(swr_init(resampler.get()), "Could not initialize the audio resampler");
            AudioFifo fifo(fastPcm ? nullptr
                                   : av_audio_fifo_alloc(encoder->sample_fmt, encoder->ch_layout.nb_channels,
                                                         std::max(encoder->frame_size * 8, 32768)));
            Frame decoded(av_frame_alloc());
            Packet packet(av_packet_alloc());
            Packet encodedPacket(av_packet_alloc());
            if ((!fastPcm && !fifo) || !decoded || !packet || !encodedPacket)
                throw std::bad_alloc();

            std::uint64_t decodedFrames = 0;
            std::int64_t timestamp = 0;
            std::vector<std::byte> pcm;
            if (fastPcm && estimatedFrames > 0)
                pcm.reserve(static_cast<std::size_t>(estimatedFrames) *
                            static_cast<std::size_t>(encoder->ch_layout.nb_channels) * sizeof(std::int16_t));
            const auto drainDecoder = [&]
            {
                while (true)
                {
                    const auto status = avcodec_receive_frame(decoder.get(), decoded.get());
                    if (status == AVERROR(EAGAIN) || status == AVERROR_EOF)
                        break;
                    RequireAv(status, "Could not decode the selected audio stream");
                    decodedFrames += static_cast<std::uint64_t>(decoded->nb_samples);
                    if (decodedFrames > MaximumFrames)
                        throw std::runtime_error("Audio source duration exceeds four hours.");
                    if (fastPcm)
                        AppendPcm(*decoded, *resampler, *encoder, pcm);
                    else
                    {
                        ConvertFrame(*decoded, *resampler, *fifo, *encoder);
                        EncodeAvailable(*fifo, *encoder, *output, *outputStream, *encodedPacket, timestamp, false);
                    }
                    av_frame_unref(decoded.get());
                }
            };

            while (av_read_frame(input.get(), packet.get()) >= 0)
            {
                if (packet->stream_index == streamIndex)
                {
                    RequireAv(avcodec_send_packet(decoder.get(), packet.get()),
                              "Could not submit an audio packet to the decoder");
                    drainDecoder();
                }
                av_packet_unref(packet.get());
            }
            RequireAv(avcodec_send_packet(decoder.get(), nullptr), "Could not flush the audio decoder");
            drainDecoder();
            if (fastPcm)
            {
                FlushPcm(*resampler, *encoder, pcm);
                const auto frames =
                    pcm.size() / (static_cast<std::size_t>(encoder->ch_layout.nb_channels) * sizeof(std::int16_t));
                if (frames == 0 || frames > MaximumFrames)
                    throw std::runtime_error("Fast audio conversion produced an invalid frame count.");
                AudioTranscodeResult result;
                result.EncodedAudio = BuildPcmWave(pcm, static_cast<std::uint32_t>(encoder->sample_rate),
                                                   static_cast<std::uint16_t>(encoder->ch_layout.nb_channels));
                result.SampleRate = static_cast<std::uint32_t>(encoder->sample_rate);
                result.Channels = static_cast<std::uint32_t>(encoder->ch_layout.nb_channels);
                result.Frames = frames;
                result.SourceCodec = decoderDefinition->name != nullptr ? decoderDefinition->name : "unknown";
                result.SourceContainer =
                    input->iformat != nullptr && input->iformat->name != nullptr ? input->iformat->name : "unknown";
                result.RuntimeEncoding = "PCM WAV";
                return result;
            }
            EncodeAvailable(*fifo, *encoder, *output, *outputStream, *encodedPacket, timestamp, true);
            RequireAv(avcodec_send_frame(encoder.get(), nullptr), "Could not flush the FLAC encoder");
            while (true)
            {
                const auto status = avcodec_receive_packet(encoder.get(), encodedPacket.get());
                if (status == AVERROR_EOF || status == AVERROR(EAGAIN))
                    break;
                RequireAv(status, "Could not receive final FLAC output");
                av_packet_rescale_ts(encodedPacket.get(), encoder->time_base, outputStream->time_base);
                encodedPacket->stream_index = outputStream->index;
                RequireAv(av_interleaved_write_frame(output.get(), encodedPacket.get()),
                          "Could not write final FLAC output");
                av_packet_unref(encodedPacket.get());
            }
            RequireAv(av_write_trailer(output.get()), "Could not finalize the FLAC output");
            if (outputState.Bytes.empty())
                throw std::runtime_error("FFmpeg produced an empty FLAC stream.");

            AudioTranscodeResult result;
            result.EncodedAudio = std::move(outputState.Bytes);
            result.SampleRate = static_cast<std::uint32_t>(encoder->sample_rate);
            result.Channels = static_cast<std::uint32_t>(encoder->ch_layout.nb_channels);
            result.Frames = static_cast<std::uint64_t>(timestamp);
            result.SourceCodec = decoderDefinition->name != nullptr ? decoderDefinition->name : "unknown";
            result.SourceContainer =
                input->iformat != nullptr && input->iformat->name != nullptr ? input->iformat->name : "unknown";
            result.RuntimeEncoding = "lossless FLAC";
            return result;
        }
    } // namespace
#endif

    AudioTranscodeBackend CreateFfmpegAudioImportBackend()
    {
#if defined(KEIRE_HAS_FFMPEG)
        return [](const AssetImportContext& context, const std::span<const std::byte> source)
        { return Transcode(context, source); };
#else
        return {};
#endif
    }
} // namespace Keire::Detail
