#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <stdexcept>

namespace Keire::Detail
{
    struct NativeAudioAttachmentTransactionEntry final
    {
        void* Source = nullptr;
        void* PreviousDestination = nullptr;
        void* Destination = nullptr;
    };

    using NativeAudioAttachFunction = bool (*)(void* source, void* destination, void* context) noexcept;

    // Locked miniaudio 0.11.25 validates null/self/bus/channel failures before mutating a connection, then returns
    // success unconditionally after attachment. Test seams must preserve that atomic-failure contract.
    inline void
    ApplyNativeAudioAttachmentTransaction(const std::span<const NativeAudioAttachmentTransactionEntry> attachments,
                                          const NativeAudioAttachFunction attach, void* context = nullptr)
    {
        if (!attach || !std::ranges::all_of(
                           attachments, [](const NativeAudioAttachmentTransactionEntry& attachment)
                           { return attachment.Source && attachment.PreviousDestination && attachment.Destination; }))
            throw std::invalid_argument("Native audio attachment transaction is invalid.");
        const std::runtime_error failure("Audio voice could not attach to its mixer bus.");
        std::size_t attached = 0;
        for (; attached < attachments.size(); ++attached)
        {
            const auto& attachment = attachments[attached];
            if (attach(attachment.Source, attachment.Destination, context))
                continue;
            bool rollbackSucceeded = true;
            for (auto rollbackIndex = attached + 1U; rollbackIndex > 0; --rollbackIndex)
            {
                const auto& rollback = attachments[rollbackIndex - 1U];
                rollbackSucceeded = attach(rollback.Source, rollback.PreviousDestination, context) && rollbackSucceeded;
            }
            assert(rollbackSucceeded && "Prevalidated native audio attachment rollback must succeed");
            (void)rollbackSucceeded;
            throw failure;
        }
    }

    class NativeAudioNodeFrameCounts final
    {
      public:
        NativeAudioNodeFrameCounts(std::uint32_t* input, std::uint32_t* output) noexcept
            : m_Input(input), m_Output(output)
        {
            assert(m_Input != nullptr);
            assert(m_Output != nullptr);
        }

        [[nodiscard]] std::uint32_t Capacity() const noexcept { return std::min(*m_Input, *m_Output); }

        void Commit(const std::uint32_t frames) noexcept
        {
            assert(frames <= Capacity());
            *m_Input = frames;
            *m_Output = frames;
        }

      private:
        // miniaudio supplies one shared input-frame count even when a node owns multiple input buses.
        std::uint32_t* m_Input = nullptr;
        std::uint32_t* m_Output = nullptr;
    };
} // namespace Keire::Detail
