#include "KeireInternal/Rendering/RenderBackendInternal.h"

#include <imgui.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace Keire::RenderBackend
{
    namespace
    {
        constexpr std::size_t MaximumImGuiTextureSnapshotBytes = 64U * 1024U * 1024U;

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
    } // namespace

    class OwnedImGuiDrawData::Impl final
    {
      public:
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
        ImVector<ImTextureData*> Textures;
        std::vector<std::unique_ptr<ImTextureData>> OwnedTextures;
        std::vector<SurfaceBinding> SurfaceBindings;
    };

    class ResolvedImGuiDrawData::Impl final
    {
      public:
        ~Impl()
        {
            for (auto* drawList : DrawLists)
                IM_DELETE(drawList);
        }

        ImDrawData Data;
        std::vector<ImDrawList*> DrawLists;
        ImVector<ImTextureData*> Textures;
        std::vector<std::unique_ptr<ImTextureData>> OwnedTextures;
    };

    ResolvedImGuiDrawData::ResolvedImGuiDrawData(std::unique_ptr<Impl> implementation)
        : m_Impl(std::move(implementation))
    {
    }

    ResolvedImGuiDrawData::~ResolvedImGuiDrawData() = default;

    ImDrawData* ResolvedImGuiDrawData::Data() noexcept { return &m_Impl->Data; }

    void ResolvedImGuiDrawData::ReleaseGpuTextures(SDL_GPUDevice* device, const bool abandon) noexcept
    {
        for (const auto& texture : m_Impl->OwnedTextures)
        {
            if (texture->GetTexID() == ImTextureID_Invalid)
                continue;
            if (!abandon && device)
            {
                auto* raw = reinterpret_cast<SDL_GPUTexture*>(static_cast<intptr_t>(texture->GetTexID()));
                if (raw)
                    SDL_ReleaseGPUTexture(device, raw);
            }
            texture->SetTexID(ImTextureID_Invalid);
            texture->SetStatus(ImTextureStatus_Destroyed);
        }
    }

    OwnedImGuiDrawData::OwnedImGuiDrawData(std::unique_ptr<Impl> implementation) : m_Impl(std::move(implementation)) {}

    OwnedImGuiDrawData::~OwnedImGuiDrawData() = default;

    std::shared_ptr<OwnedImGuiDrawData>
    OwnedImGuiDrawData::Capture(ImDrawData* drawData, const std::span<const CapturedSurfaceTextureBinding> surfaces)
    {
        if (!drawData)
            return {};
        if (!drawData->Valid)
            throw std::invalid_argument("Dear ImGui draw data must be finalized before asynchronous capture.");
        if (drawData->CmdListsCount != drawData->CmdLists.Size || drawData->CmdLists.Size < 0 ||
            drawData->CmdLists.Size > 4096 || drawData->TotalVtxCount < 0 || drawData->TotalIdxCount < 0 ||
            drawData->TotalVtxCount > 16'777'216 || drawData->TotalIdxCount > 33'554'432)
        {
            throw std::length_error("Dear ImGui draw data exceeds the asynchronous frame-packet bounds.");
        }

        auto implementation = std::make_unique<Impl>();
        implementation->Data.Valid = true;
        implementation->Data.DisplayPos = drawData->DisplayPos;
        implementation->Data.DisplaySize = drawData->DisplaySize;
        implementation->Data.FramebufferScale = drawData->FramebufferScale;
        implementation->Data.OwnerViewport = nullptr;
        std::unordered_map<const ImTextureData*, ImTextureData*> textureCopies;
        std::size_t copiedTextureBytes = 0;
        const auto copyTexture = [&](const ImTextureData* source) -> ImTextureData*
        {
            if (!source)
                throw std::invalid_argument("Dear ImGui draw data contains a null texture-data entry.");
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
            auto copy = std::make_unique<ImTextureData>();
            copy->Create(source->Format, source->Width, source->Height);
            std::memcpy(copy->Pixels, source->Pixels, byteCount);
            copy->UniqueID = source->UniqueID;
            copy->UseColors = source->UseColors;
            copy->SetTexID(ImTextureID_Invalid);
            copy->SetStatus(ImTextureStatus_WantCreate);
            auto* result = copy.get();
            implementation->OwnedTextures.push_back(std::move(copy));
            textureCopies.emplace(source, result);
            implementation->Textures.push_back(result);
            return result;
        };
        if (drawData->Textures)
        {
            implementation->Textures.reserve(drawData->Textures->Size);
            implementation->OwnedTextures.reserve(static_cast<std::size_t>(drawData->Textures->Size));
            for (const auto* texture : *drawData->Textures)
                (void)copyTexture(texture);
        }
        implementation->Data.Textures = implementation->Textures.empty() ? nullptr : &implementation->Textures;

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
                    // No custom callback state is retained in normalized packets, so the backend's initial render-state
                    // setup already satisfies the reset command.
                    command.UserCallback = nullptr;
                    command.UserCallbackData = nullptr;
                    command.UserCallbackDataSize = 0;
                    command.UserCallbackDataOffset = -1;
                    command.ElemCount = 0;
                }

                if (command.TexRef._TexData)
                {
                    command.TexRef = copyTexture(command.TexRef._TexData)->GetTexRef();
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

        return std::shared_ptr<OwnedImGuiDrawData>(new OwnedImGuiDrawData(std::move(implementation)));
    }

    std::shared_ptr<ResolvedImGuiDrawData> OwnedImGuiDrawData::ResolveForRender(
        const std::function<std::uintptr_t(const RenderSurfaceToken&)>& resolveTexture) const
    {
        auto resolved = std::make_unique<ResolvedImGuiDrawData::Impl>();
        resolved->Data.Valid = true;
        resolved->Data.DisplayPos = m_Impl->Data.DisplayPos;
        resolved->Data.DisplaySize = m_Impl->Data.DisplaySize;
        resolved->Data.FramebufferScale = m_Impl->Data.FramebufferScale;
        resolved->Data.OwnerViewport = nullptr;

        std::unordered_map<const ImTextureData*, ImTextureData*> textureCopies;
        std::size_t copiedTextureBytes = 0;
        resolved->Textures.reserve(static_cast<int>(m_Impl->OwnedTextures.size()));
        resolved->OwnedTextures.reserve(m_Impl->OwnedTextures.size());
        for (const auto& source : m_Impl->OwnedTextures)
        {
            const auto byteCount = CheckedTextureByteCount(*source);
            if (copiedTextureBytes > MaximumImGuiTextureSnapshotBytes ||
                byteCount > MaximumImGuiTextureSnapshotBytes - copiedTextureBytes)
                throw std::length_error("Dear ImGui texture snapshots exceed the 64 MiB frame-packet bound.");
            copiedTextureBytes += byteCount;
            auto copy = std::make_unique<ImTextureData>();
            copy->Create(source->Format, source->Width, source->Height);
            std::memcpy(copy->Pixels, source->Pixels, byteCount);
            copy->UniqueID = source->UniqueID;
            copy->UseColors = source->UseColors;
            copy->SetTexID(ImTextureID_Invalid);
            copy->SetStatus(ImTextureStatus_WantCreate);
            auto* copiedTexture = copy.get();
            textureCopies.emplace(source.get(), copiedTexture);
            resolved->OwnedTextures.push_back(std::move(copy));
            resolved->Textures.push_back(copiedTexture);
        }
        resolved->Data.Textures = resolved->Textures.empty() ? nullptr : &resolved->Textures;

        resolved->DrawLists.reserve(m_Impl->DrawLists.size());
        for (const auto* source : m_Impl->DrawLists)
        {
            auto* clone = source->CloneOutput();
            for (auto& command : clone->CmdBuffer)
            {
                if (!command.TexRef._TexData)
                    continue;
                const auto texture = textureCopies.find(command.TexRef._TexData);
                if (texture == textureCopies.end())
                {
                    IM_DELETE(clone);
                    throw std::logic_error("Captured Dear ImGui texture snapshot is missing from the render view.");
                }
                command.TexRef = texture->second->GetTexRef();
            }
            resolved->DrawLists.push_back(clone);
            AppendFinalizedDrawList(resolved->Data, clone);
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
