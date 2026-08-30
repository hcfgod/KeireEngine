#include "KeireInternal/Rendering/RenderBackendInternal.h"

#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Keire::RenderBackend
{
    RenderFramePacket::RenderFramePacket() = default;

    RenderFramePacket::~RenderFramePacket() = default;

    namespace
    {
        constexpr std::size_t MaximumImGuiTextureSnapshotBytes = 64U * 1024U * 1024U;

        struct PendingTextureAcknowledgement final
        {
            ImTextureData* Texture = nullptr;
            ImTextureStatus Request = ImTextureStatus_OK;
            ImTextureID PreviousId = ImTextureID_Invalid;
            ImTextureID LogicalId = ImTextureID_Invalid;
        };

        [[nodiscard]] ImTextureID AllocateLogicalTextureId()
        {
            static std::atomic<std::uint64_t> nextId{1U};
            const auto id = nextId.fetch_add(1U, std::memory_order_relaxed);
            if (id == 0U || id == (std::numeric_limits<std::uint64_t>::max)())
                throw std::overflow_error("Dear ImGui logical texture identifiers are exhausted.");
            return static_cast<ImTextureID>(id);
        }

        [[nodiscard]] std::size_t CheckedTextureByteCount(const ImTextureData& texture)
        {
            if (texture.Width <= 0 || texture.Height <= 0 || texture.BytesPerPixel <= 0)
                throw std::invalid_argument("Dear ImGui texture snapshot dimensions must be positive.");

            const auto width = static_cast<std::size_t>(texture.Width);
            const auto height = static_cast<std::size_t>(texture.Height);
            const auto bytesPerPixel = static_cast<std::size_t>(texture.BytesPerPixel);
            if (width > MaximumImGuiTextureSnapshotBytes / bytesPerPixel)
                throw std::length_error("Dear ImGui texture snapshots exceed the 64 MiB frame-packet bound.");
            const auto rowBytes = width * bytesPerPixel;
            if (height > MaximumImGuiTextureSnapshotBytes / rowBytes)
                throw std::length_error("Dear ImGui texture snapshots exceed the 64 MiB frame-packet bound.");
            return rowBytes * height;
        }

        void AppendFinalizedDrawList(ImDrawData& drawData, ImDrawList* drawList)
        {
            if (!drawList)
                throw std::invalid_argument("Dear ImGui draw data contains a null draw list.");
            if (drawList->VtxBuffer.Size < 0 || drawList->IdxBuffer.Size < 0)
                throw std::invalid_argument("Dear ImGui finalized draw-list sizes cannot be negative.");
            if (drawList->VtxBuffer.Size > (std::numeric_limits<int>::max)() - drawData.TotalVtxCount ||
                drawList->IdxBuffer.Size > (std::numeric_limits<int>::max)() - drawData.TotalIdxCount)
            {
                throw std::length_error("Dear ImGui finalized draw-list totals exceed the frame-packet bounds.");
            }

            // CloneOutput() deliberately copies only finalized output buffers. Its mutable construction cursors are
            // null, so ImDrawData::AddDrawList() is not a valid way to assemble the immutable render-thread view.
            drawData.CmdLists.push_back(drawList);
            drawData.CmdListsCount = drawData.CmdLists.Size;
            drawData.TotalVtxCount += drawList->VtxBuffer.Size;
            drawData.TotalIdxCount += drawList->IdxBuffer.Size;
        }

        [[nodiscard]] SDL_GPUTexture* TextureFromId(const ImTextureID texture) noexcept
        {
            return reinterpret_cast<SDL_GPUTexture*>(static_cast<std::intptr_t>(texture));
        }

        [[nodiscard]] ImTextureID TextureToId(SDL_GPUTexture* texture) noexcept
        {
            return static_cast<ImTextureID>(reinterpret_cast<std::intptr_t>(texture));
        }
    } // namespace

    class ImGuiTextureCache::Impl final
    {
      public:
        struct Entry final
        {
            ImTextureID LogicalId = ImTextureID_Invalid;
            SDL_GPUTexture* Texture = nullptr;
            std::uint32_t DeviceGeneration = 0;
        };

        [[nodiscard]] auto Find(const ImTextureID logicalId)
        {
            return std::lower_bound(Entries.begin(), Entries.end(), logicalId,
                                    [](const Entry& entry, const ImTextureID id) { return entry.LogicalId < id; });
        }

        [[nodiscard]] auto Find(const ImTextureID logicalId) const
        {
            return std::lower_bound(Entries.begin(), Entries.end(), logicalId,
                                    [](const Entry& entry, const ImTextureID id) { return entry.LogicalId < id; });
        }

        std::vector<Entry> Entries;
    };

    ImGuiTextureCache::ImGuiTextureCache() : m_Impl(std::make_unique<Impl>()) {}

    ImGuiTextureCache::~ImGuiTextureCache() = default;

    void ImGuiTextureCache::ReleaseGpuTextures(SDL_GPUDevice* device, const bool abandon) noexcept
    {
        for (auto& entry : m_Impl->Entries)
        {
            if (!abandon && device && entry.Texture)
                SDL_ReleaseGPUTexture(device, entry.Texture);
            entry.Texture = nullptr;
            entry.DeviceGeneration = 0;
        }
    }

#if defined(KEIRE_ENABLE_TEST_HOOKS)
    std::size_t ImGuiTextureCache::TextureCountForTest() const noexcept { return m_Impl->Entries.size(); }

    std::size_t ImGuiTextureCache::GpuTextureCountForTest(const std::uint32_t deviceGeneration) const noexcept
    {
        return static_cast<std::size_t>(
            std::ranges::count_if(m_Impl->Entries, [deviceGeneration](const Impl::Entry& entry)
                                  { return entry.Texture && entry.DeviceGeneration == deviceGeneration; }));
    }
#endif

    class OwnedImGuiDrawData::Impl final
    {
      public:
        struct TextureSnapshot final
        {
            ImTextureID LogicalId = ImTextureID_Invalid;
            ImTextureStatus Request = ImTextureStatus_OK;
            int UniqueId = 0;
            ImTextureFormat Format = ImTextureFormat_RGBA32;
            int Width = 0;
            int Height = 0;
            int BytesPerPixel = 0;
            ImTextureRect UsedRect{};
            ImTextureRect UpdateRect{};
            std::vector<ImTextureRect> Updates;
            bool UseColors = false;
            std::vector<unsigned char> Pixels;
        };

        struct TextureBinding final
        {
            std::size_t DrawList = 0;
            std::size_t Command = 0;
            ImTextureID LogicalId = ImTextureID_Invalid;
        };

        struct SurfaceBinding final
        {
            std::size_t DrawList = 0;
            std::size_t Command = 0;
            RenderSurfaceToken Surface;
        };

        ~Impl()
        {
            for (auto* drawList : DrawLists)
                IM_DELETE(drawList);
        }

        ImDrawData Data;
        std::vector<ImDrawList*> DrawLists;
        std::vector<TextureSnapshot> Textures;
        std::vector<TextureBinding> TextureBindings;
        std::vector<SurfaceBinding> SurfaceBindings;
        std::vector<ImTextureID> DestroyedTextureIds;
    };

    class ResolvedImGuiDrawData::Impl final
    {
      public:
        struct Upload final
        {
            ImTextureID LogicalId = ImTextureID_Invalid;
            std::unique_ptr<ImTextureData> Texture;
            bool OwnsGpuTexture = false;
        };

        ~Impl()
        {
            for (auto* drawList : DrawLists)
                IM_DELETE(drawList);
        }

        ImDrawData Data;
        std::vector<ImDrawList*> DrawLists;
        ImVector<ImTextureData*> Textures;
        std::vector<Upload> Uploads;
        std::vector<ImTextureID> DestroyedTextureIds;
        std::vector<SDL_GPUTexture*> RetiredGpuTextures;
        bool Committed = false;
    };

    ResolvedImGuiDrawData::ResolvedImGuiDrawData(std::unique_ptr<Impl> implementation)
        : m_Impl(std::move(implementation))
    {
    }

    ResolvedImGuiDrawData::~ResolvedImGuiDrawData() = default;

    ImDrawData* ResolvedImGuiDrawData::Data() noexcept { return &m_Impl->Data; }

    void ResolvedImGuiDrawData::CommitGpuTextures(ImGuiTextureCache& cache,
                                                  const std::uint32_t deviceGeneration) noexcept
    {
        if (m_Impl->Committed)
            return;
        m_Impl->Committed = true;

        for (auto& upload : m_Impl->Uploads)
        {
            if (!upload.OwnsGpuTexture)
                continue;
            auto* texture = TextureFromId(upload.Texture->GetTexID());
            if (!texture || upload.Texture->Status != ImTextureStatus_OK)
                continue;

            auto found = cache.m_Impl->Find(upload.LogicalId);
            if (found == cache.m_Impl->Entries.end() || found->LogicalId != upload.LogicalId)
            {
                found = cache.m_Impl->Entries.insert(
                    found, {.LogicalId = upload.LogicalId, .Texture = texture, .DeviceGeneration = deviceGeneration});
            }
            else
            {
                if (found->Texture && found->Texture != texture && found->DeviceGeneration == deviceGeneration)
                    m_Impl->RetiredGpuTextures.push_back(found->Texture);
                found->Texture = texture;
                found->DeviceGeneration = deviceGeneration;
            }
            upload.OwnsGpuTexture = false;
        }

        for (const auto logicalId : m_Impl->DestroyedTextureIds)
        {
            const auto found = cache.m_Impl->Find(logicalId);
            if (found == cache.m_Impl->Entries.end() || found->LogicalId != logicalId)
                continue;
            if (found->Texture)
                m_Impl->RetiredGpuTextures.push_back(found->Texture);
            cache.m_Impl->Entries.erase(found);
        }
    }

    void ResolvedImGuiDrawData::ReleaseGpuTextures(SDL_GPUDevice* device, const bool abandon) noexcept
    {
        for (auto& upload : m_Impl->Uploads)
        {
            if (upload.OwnsGpuTexture && upload.Texture->GetTexID() != ImTextureID_Invalid)
            {
                if (!abandon && device)
                {
                    if (auto* raw = TextureFromId(upload.Texture->GetTexID()))
                        SDL_ReleaseGPUTexture(device, raw);
                }
                upload.OwnsGpuTexture = false;
            }
            upload.Texture->SetTexID(ImTextureID_Invalid);
            upload.Texture->WantDestroyNextFrame = true;
            upload.Texture->SetStatus(ImTextureStatus_Destroyed);
        }
        for (auto* texture : m_Impl->RetiredGpuTextures)
            if (!abandon && device && texture)
                SDL_ReleaseGPUTexture(device, texture);
        m_Impl->RetiredGpuTextures.clear();
    }

#if defined(KEIRE_ENABLE_TEST_HOOKS)
    std::size_t ResolvedImGuiDrawData::PendingGpuTextureRetirementCountForTest() const noexcept
    {
        return m_Impl->RetiredGpuTextures.size();
    }
#endif

    OwnedImGuiDrawData::OwnedImGuiDrawData(std::unique_ptr<Impl> implementation) : m_Impl(std::move(implementation)) {}

    OwnedImGuiDrawData::~OwnedImGuiDrawData() = default;

    std::shared_ptr<OwnedImGuiDrawData>
    OwnedImGuiDrawData::Capture(ImDrawData* drawData, const std::span<const CapturedSurfaceTextureBinding> surfaces,
                                const std::span<const std::uintptr_t> retiredTextureIds)
    {
        if (!drawData && retiredTextureIds.empty())
            return {};
        if (drawData && !drawData->Valid)
            throw std::invalid_argument("Dear ImGui draw data must be finalized before asynchronous capture.");
        if (drawData && (drawData->CmdListsCount != drawData->CmdLists.Size || drawData->CmdLists.Size < 0 ||
                         drawData->CmdLists.Size > 4096 || drawData->TotalVtxCount < 0 || drawData->TotalIdxCount < 0 ||
                         drawData->TotalVtxCount > 16'777'216 || drawData->TotalIdxCount > 33'554'432))
        {
            throw std::length_error("Dear ImGui draw data exceeds the asynchronous frame-packet bounds.");
        }
        constexpr std::size_t maximumTextureRetirements = 65'536U;
        if (retiredTextureIds.size() > maximumTextureRetirements)
            throw std::length_error("Dear ImGui texture retirements exceed the asynchronous frame-packet bound.");

        auto implementation = std::make_unique<Impl>();
        implementation->Data.Valid = true;
        if (drawData)
        {
            implementation->Data.DisplayPos = drawData->DisplayPos;
            implementation->Data.DisplaySize = drawData->DisplaySize;
            implementation->Data.FramebufferScale = drawData->FramebufferScale;
        }
        implementation->Data.OwnerViewport = nullptr;
        implementation->DestroyedTextureIds.reserve(retiredTextureIds.size());
        for (const auto retiredTextureId : retiredTextureIds)
        {
            const auto logicalId = static_cast<ImTextureID>(retiredTextureId);
            if (logicalId != ImTextureID_Invalid)
                implementation->DestroyedTextureIds.push_back(logicalId);
        }
        std::ranges::sort(implementation->DestroyedTextureIds);
        implementation->DestroyedTextureIds.erase(
            std::unique(implementation->DestroyedTextureIds.begin(), implementation->DestroyedTextureIds.end()),
            implementation->DestroyedTextureIds.end());

        if (!drawData)
            return std::shared_ptr<OwnedImGuiDrawData>(new OwnedImGuiDrawData(std::move(implementation)));

        std::unordered_map<const ImTextureData*, ImTextureID> textureCopies;
        std::vector<PendingTextureAcknowledgement> pendingTextureAcknowledgements;
        std::size_t copiedTextureBytes = 0;
        const auto copyTexture = [&](ImTextureData* source) -> ImTextureID
        {
            if (!source)
                throw std::invalid_argument("Dear ImGui draw data contains a null referenced texture.");
            if (const auto found = textureCopies.find(source); found != textureCopies.end())
                return found->second;
            if (source->Format != ImTextureFormat_RGBA32 || source->Width <= 0 || source->Height <= 0 ||
                source->BytesPerPixel != 4 || !source->Pixels)
            {
                throw std::invalid_argument(
                    "Dear ImGui asynchronous packets require owned RGBA32 texture pixels; borrowed backend-only "
                    "texture data is unsupported.");
            }
            const auto byteCount = CheckedTextureByteCount(*source);
            if (copiedTextureBytes > MaximumImGuiTextureSnapshotBytes ||
                byteCount > MaximumImGuiTextureSnapshotBytes - copiedTextureBytes)
                throw std::length_error("Dear ImGui texture snapshots exceed the 64 MiB frame-packet bound.");
            copiedTextureBytes += byteCount;

            const auto previousId = source->GetTexID();
            const auto logicalId = previousId == ImTextureID_Invalid ? AllocateLogicalTextureId() : previousId;
            auto request = source->Status;
            if (request == ImTextureStatus_Destroyed ||
                (request == ImTextureStatus_OK && previousId == ImTextureID_Invalid))
                request = ImTextureStatus_WantCreate;
            if (request != ImTextureStatus_OK && request != ImTextureStatus_WantCreate &&
                request != ImTextureStatus_WantUpdates && request != ImTextureStatus_WantDestroy)
            {
                throw std::invalid_argument("Dear ImGui referenced texture has an unsupported lifecycle state.");
            }

            Impl::TextureSnapshot snapshot;
            snapshot.LogicalId = logicalId;
            snapshot.Request = request;
            snapshot.UniqueId = source->UniqueID;
            snapshot.Format = source->Format;
            snapshot.Width = source->Width;
            snapshot.Height = source->Height;
            snapshot.BytesPerPixel = source->BytesPerPixel;
            snapshot.UsedRect = source->UsedRect;
            snapshot.UpdateRect = source->UpdateRect;
            snapshot.Updates.assign(source->Updates.begin(), source->Updates.end());
            snapshot.UseColors = source->UseColors;
            snapshot.Pixels.assign(source->Pixels, source->Pixels + byteCount);
            implementation->Textures.push_back(std::move(snapshot));
            textureCopies.emplace(source, logicalId);
            pendingTextureAcknowledgements.push_back({source, source->Status, previousId, logicalId});
            if (request == ImTextureStatus_WantDestroy)
                implementation->DestroyedTextureIds.push_back(logicalId);
            return logicalId;
        };

        const auto resetCallback = ImGui::GetPlatformIO().DrawCallback_ResetRenderState;
        implementation->DrawLists.reserve(static_cast<std::size_t>(drawData->CmdLists.Size));
        for (int listIndex = 0; listIndex < drawData->CmdLists.Size; ++listIndex)
        {
            const auto* source = drawData->CmdLists[listIndex];
            if (!source)
                throw std::invalid_argument("Dear ImGui draw data contains a null draw list.");
            auto* clone = source->CloneOutput();
            implementation->DrawLists.push_back(clone);
            AppendFinalizedDrawList(implementation->Data, clone);

            for (int commandIndex = 0; commandIndex < clone->CmdBuffer.Size; ++commandIndex)
            {
                auto& command = clone->CmdBuffer[commandIndex];
                if (command.UserCallback)
                {
                    if (command.UserCallback != resetCallback &&
                        command.UserCallback != ImDrawCallback_ResetRenderState)
                    {
                        throw std::invalid_argument(
                            "Dear ImGui user callbacks cannot be executed from an asynchronous render packet.");
                    }
                    command.UserCallback = nullptr;
                    command.UserCallbackData = nullptr;
                    command.UserCallbackDataSize = 0;
                    command.UserCallbackDataOffset = -1;
                    command.ElemCount = 0;
                }

                if (command.TexRef._TexData)
                {
                    const auto logicalId = copyTexture(command.TexRef._TexData);
                    implementation->TextureBindings.push_back(
                        {static_cast<std::size_t>(listIndex), static_cast<std::size_t>(commandIndex), logicalId});
                    command.TexRef = {};
                    continue;
                }
                if (command.GetTexID() == ImTextureID_Invalid)
                    continue;
                const auto textureIdentity = static_cast<std::uintptr_t>(command.GetTexID());
                const auto surface =
                    std::ranges::find_if(surfaces, [textureIdentity](const auto& candidate)
                                         { return candidate.Surface && candidate.TextureIdentity == textureIdentity; });
                if (surface == surfaces.end())
                {
                    throw std::invalid_argument(
                        "Dear ImGui draw data contains an unregistered raw GPU texture; use a UI image or render "
                        "surface lease.");
                }
                implementation->SurfaceBindings.push_back(
                    {static_cast<std::size_t>(listIndex), static_cast<std::size_t>(commandIndex), surface->Surface});
                command.TexRef = {};
            }
        }
        if (implementation->Data.TotalVtxCount != drawData->TotalVtxCount ||
            implementation->Data.TotalIdxCount != drawData->TotalIdxCount)
        {
            throw std::invalid_argument("Dear ImGui finalized draw-data totals do not match its output buffers.");
        }

        if (drawData->Textures)
        {
            std::unordered_set<ImTextureID> destroyedIds(implementation->DestroyedTextureIds.begin(),
                                                         implementation->DestroyedTextureIds.end());
            for (auto* texture : *drawData->Textures)
            {
                if (!texture || textureCopies.contains(texture) || texture->Status != ImTextureStatus_WantDestroy)
                    continue;
                const auto logicalId = texture->GetTexID();
                pendingTextureAcknowledgements.push_back({texture, ImTextureStatus_WantDestroy, logicalId, logicalId});
                if (logicalId != ImTextureID_Invalid && destroyedIds.insert(logicalId).second)
                    implementation->DestroyedTextureIds.push_back(logicalId);
            }
        }

        auto captured = std::shared_ptr<OwnedImGuiDrawData>(new OwnedImGuiDrawData(std::move(implementation)));
        for (const auto& acknowledgement : pendingTextureAcknowledgements)
        {
            if (acknowledgement.Texture->Status != acknowledgement.Request ||
                acknowledgement.Texture->GetTexID() != acknowledgement.PreviousId)
                continue;
            if (acknowledgement.Request == ImTextureStatus_WantDestroy)
            {
                acknowledgement.Texture->WantDestroyNextFrame = true;
                acknowledgement.Texture->BackendUserData = nullptr;
                acknowledgement.Texture->SetTexID(ImTextureID_Invalid);
                acknowledgement.Texture->SetStatus(ImTextureStatus_Destroyed);
                continue;
            }
            acknowledgement.Texture->SetTexID(acknowledgement.LogicalId);
            if (acknowledgement.Request == ImTextureStatus_WantCreate ||
                acknowledgement.Request == ImTextureStatus_WantUpdates ||
                acknowledgement.Request == ImTextureStatus_Destroyed)
            {
                acknowledgement.Texture->SetStatus(ImTextureStatus_OK);
            }
        }
        return captured;
    }

    std::shared_ptr<ResolvedImGuiDrawData> OwnedImGuiDrawData::ResolveForRender(
        ImGuiTextureCache& cache, const std::uint32_t deviceGeneration,
        const std::function<std::uintptr_t(const RenderSurfaceToken&)>& resolveTexture) const
    {
        auto resolved = std::make_unique<ResolvedImGuiDrawData::Impl>();
        resolved->Data.Valid = true;
        resolved->Data.DisplayPos = m_Impl->Data.DisplayPos;
        resolved->Data.DisplaySize = m_Impl->Data.DisplaySize;
        resolved->Data.FramebufferScale = m_Impl->Data.FramebufferScale;
        resolved->Data.OwnerViewport = nullptr;
        resolved->Uploads.reserve(m_Impl->Textures.size());
        resolved->Textures.reserve(static_cast<int>(m_Impl->Textures.size()));
        resolved->RetiredGpuTextures.reserve(m_Impl->Textures.size() + m_Impl->DestroyedTextureIds.size());
        cache.m_Impl->Entries.reserve(cache.m_Impl->Entries.size() + m_Impl->Textures.size());

        std::unordered_map<ImTextureID, ImTextureData*> uploadTextures;
        std::unordered_map<ImTextureID, SDL_GPUTexture*> stableTextures;
        uploadTextures.reserve(m_Impl->Textures.size());
        stableTextures.reserve(m_Impl->Textures.size());
        for (const auto& snapshot : m_Impl->Textures)
        {
            const auto found = cache.m_Impl->Find(snapshot.LogicalId);
            auto* stable = found != cache.m_Impl->Entries.end() && found->LogicalId == snapshot.LogicalId &&
                                   found->DeviceGeneration == deviceGeneration
                               ? found->Texture
                               : nullptr;
            const bool replace = snapshot.Request == ImTextureStatus_WantCreate;
            const bool update = snapshot.Request == ImTextureStatus_WantUpdates && stable;
            if (!replace && !update && stable)
            {
                stableTextures.emplace(snapshot.LogicalId, stable);
                continue;
            }

            auto texture = std::make_unique<ImTextureData>();
            texture->Create(snapshot.Format, snapshot.Width, snapshot.Height);
            std::memcpy(texture->Pixels, snapshot.Pixels.data(), snapshot.Pixels.size());
            texture->UniqueID = snapshot.UniqueId;
            texture->UseColors = snapshot.UseColors;
            texture->UsedRect = snapshot.UsedRect;
            texture->UpdateRect = snapshot.UpdateRect;
            for (const auto& rectangle : snapshot.Updates)
                texture->Updates.push_back(rectangle);
            texture->SetTexID(update ? TextureToId(stable) : ImTextureID_Invalid);
            texture->SetStatus(update ? ImTextureStatus_WantUpdates : ImTextureStatus_WantCreate);
            auto* upload = texture.get();
            resolved->Uploads.push_back(
                {.LogicalId = snapshot.LogicalId, .Texture = std::move(texture), .OwnsGpuTexture = !update});
            resolved->Textures.push_back(upload);
            uploadTextures.emplace(snapshot.LogicalId, upload);
        }
        resolved->Data.Textures = resolved->Textures.empty() ? nullptr : &resolved->Textures;
        resolved->DestroyedTextureIds = m_Impl->DestroyedTextureIds;

        resolved->DrawLists.reserve(m_Impl->DrawLists.size());
        for (const auto* source : m_Impl->DrawLists)
        {
            auto* clone = source->CloneOutput();
            resolved->DrawLists.push_back(clone);
            AppendFinalizedDrawList(resolved->Data, clone);
        }
        for (const auto& binding : m_Impl->TextureBindings)
        {
            auto& command = resolved->DrawLists[binding.DrawList]->CmdBuffer[static_cast<int>(binding.Command)];
            if (const auto upload = uploadTextures.find(binding.LogicalId); upload != uploadTextures.end())
            {
                command.TexRef = upload->second->GetTexRef();
                continue;
            }
            if (const auto stable = stableTextures.find(binding.LogicalId); stable != stableTextures.end())
            {
                command.TexRef = ImTextureRef(TextureToId(stable->second));
                continue;
            }
            throw std::logic_error("Captured Dear ImGui texture snapshot is missing from the render view.");
        }
        for (const auto& binding : m_Impl->SurfaceBindings)
        {
            auto& command = resolved->DrawLists[binding.DrawList]->CmdBuffer[static_cast<int>(binding.Command)];
            const auto textureIdentity = resolveTexture ? resolveTexture(binding.Surface) : 0U;
            command.TexRef = textureIdentity ? ImTextureRef(static_cast<ImTextureID>(textureIdentity)) : ImTextureRef{};
        }
        return std::shared_ptr<ResolvedImGuiDrawData>(new ResolvedImGuiDrawData(std::move(resolved)));
    }
} // namespace Keire::RenderBackend
