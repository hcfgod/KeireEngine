#include "KeireClient/Editor/ShaderGraphPanel.h"
#include "KeireClient/Editor/ShaderGraphPreview.h"

#include <algorithm>
#include <atomic>
#include <exception>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace KeireEditor
{
    ShaderGraphPanel::~ShaderGraphPanel() noexcept
    {
        m_PreviewCancellation->fetch_add(1, std::memory_order_release);
        if (m_PreviewRender)
        {
            m_PreviewRender.Cancel();
            (void)m_PreviewRender.Wait();
        }
        if (m_JobScope)
        {
            m_JobScope->Cancel();
            m_JobScope->Wait();
        }
        if (m_OwnJobSystem && m_JobSystem)
            m_JobSystem->Close();
    }

    void ShaderGraphPanel::SetJobSystem(Keire::Ref<Keire::JobSystem> jobs)
    {
        if (m_PreviewRender || m_JobScope)
            throw std::logic_error("Shader Graph preview jobs are already configured.");
        if (!jobs)
            throw std::invalid_argument("Shader Graph preview job system is unavailable.");
        m_JobSystem = std::move(jobs);
    }

    void ShaderGraphPanel::DrawPreview(Keire::UiFrame& ui)
    {
        const auto& theme = m_Controller.ShaderGraphTheme();
        if (m_PreviewRender && m_PreviewRender.IsComplete())
        {
            (void)m_PreviewRender.Wait();
            try
            {
                m_PreviewRender.RethrowIfFailed();
                std::optional<PreviewRenderResult> result;
                {
                    std::scoped_lock lock(m_PreviewRenderState->Mutex);
                    result = std::move(m_PreviewRenderState->Result);
                }
                if (result && result->Generation == m_PreviewGeneration)
                {
                    if (result->Error.empty())
                    {
                        m_PreviewImage = ui.CreateImage(result->Width, result->Height, result->Pixels);
                        if (!result->FinalQuality)
                        {
                            m_PreviewRefinement = true;
                            m_PreviewDirty = true;
                        }
                    }
                    else
                        Report(std::move(result->Error));
                }
            }
            catch (const std::exception& error)
            {
                Report(error.what());
            }
            m_PreviewRender = {};
            m_PreviewRenderState.reset();
        }
        ui.TextColored(theme.Accent, "LIVE SHADER PREVIEW");
        auto preview = m_Controller.ShaderGraphState().PreviewSettings();
        bool previewChanged = false;
        previewChanged |= ui.SliderFloat("Exposure", preview.Exposure, 0.1F, 4.0F);
        previewChanged |= ui.SliderFloat("Environment", preview.EnvironmentIntensity, 0.0F, 4.0F);
        previewChanged |= ui.SliderFloat("Rotation", preview.RotationDegrees, -180.0F, 180.0F);
        if (previewChanged)
            m_Controller.ShaderGraphState().SetPreviewSettings(preview);
        if (m_PreviewProperties.empty() && !m_Controller.ShaderGraphState().LastGoodCompilation())
        {
            ui.TextColored(theme.MutedText, "A preview appears after the graph compiles successfully.");
            return;
        }
        constexpr std::uint32_t previewWidth = 216;
        constexpr std::uint32_t previewHeight = 216;
        if (previewWidth != m_PreviewWidth || previewHeight != m_PreviewHeight)
        {
            m_PreviewWidth = previewWidth;
            m_PreviewHeight = previewHeight;
            m_PreviewRefinement = false;
            m_PreviewDirty = true;
            if (++m_PreviewGeneration == 0)
                ++m_PreviewGeneration;
            m_PreviewCancellation->store(m_PreviewGeneration, std::memory_order_release);
        }
        if (m_PreviewDirty && !m_PreviewRender)
        {
            try
            {
                EnsureJobScope();
                Keire::Ref<const Keire::MeshAsset> customMesh;
                if (m_PreviewSettings.Mesh == Keire::ShaderGraphPreviewMesh::Custom)
                {
                    customMesh = m_Controller.ResolveShaderGraphPreviewMesh(m_PreviewSettings.CustomMesh);
                    if (!customMesh)
                    {
                        ui.TextColored(theme.MutedText, "Loading the custom preview mesh...");
                        return;
                    }
                }
                const auto& lastGoodDefinition = m_Controller.ShaderGraphState().LastGoodDefinition();
                if (!lastGoodDefinition)
                    return;
                const auto generation = m_PreviewGeneration;
                const bool finalQuality = m_PreviewRefinement;
                const auto width = finalQuality ? m_PreviewWidth : std::max(96U, m_PreviewWidth / 2U);
                const auto height = finalQuality ? m_PreviewHeight : std::max(64U, m_PreviewHeight / 2U);
                auto definition =
                    Keire::ExpandShaderGraphFunctions(*lastGoodDefinition, [this](const Keire::AssetId asset)
                                                      { return m_Controller.ResolveShaderGraphFunction(asset); });
                auto properties = m_PreviewProperties;
                auto settings = m_PreviewSettings;
                auto cancellation = m_PreviewCancellation;
                m_PreviewRenderState = std::make_shared<PreviewRenderState>();
                const auto state = m_PreviewRenderState;
                m_PreviewRender = m_JobScope->Submit(
                    {.Name = "Render Shader Graph preview",
                     .Priority = Keire::JobPriority::Low,
                     .Class = Keire::JobClass::Compute,
                     .Domain = Keire::JobDomain::Tooling},
                    [generation, width, height, definition = std::move(definition), properties = std::move(properties),
                     settings = std::move(settings), customMesh = std::move(customMesh),
                     cancellation = std::move(cancellation), finalQuality, state](Keire::JobContext& context) mutable
                    {
                        PreviewRenderResult result{
                            .Generation = generation, .Width = width, .Height = height, .FinalQuality = finalQuality};
                        const auto jobStop = context.StopToken();
                        try
                        {
                            result.Pixels = RenderShaderGraphPreview({
                                .Output = definition.Output,
                                .Mesh = settings.Mesh,
                                .CustomMesh = std::move(customMesh),
                                .Definition = &definition,
                                .Properties = properties,
                                .Width = width,
                                .Height = height,
                                .Exposure = settings.Exposure,
                                .EnvironmentIntensity = settings.EnvironmentIntensity,
                                .RotationDegrees = settings.RotationDegrees,
                                .CancellationRequested =
                                    [cancellation, generation, jobStop]
                                {
                                    return jobStop.stop_requested() ||
                                           cancellation->load(std::memory_order_acquire) != generation;
                                },
                            });
                        }
                        catch (const std::exception& error)
                        {
                            result.Error = error.what();
                        }
                        if (!context.StopRequested())
                        {
                            std::scoped_lock lock(state->Mutex);
                            state->Result = std::move(result);
                        }
                    });
                m_PreviewRefinement = false;
                m_PreviewDirty = false;
            }
            catch (const std::exception& error)
            {
                Report(error.what());
                m_PreviewDirty = false;
            }
        }
        if (m_PreviewDirty || m_PreviewRender)
            ui.TextColored(theme.MutedText, "Updating preview...");
        if (m_PreviewImage)
            ui.Image(m_PreviewImage, {static_cast<float>(m_PreviewWidth), static_cast<float>(m_PreviewHeight)});
        else
            ui.TextColored(theme.Warning, "The live preview is unavailable.");
    }

    void ShaderGraphPanel::EnsureJobScope()
    {
        if (m_JobScope)
            return;
        if (!m_JobSystem)
        {
            Keire::JobSystemSpecification specification;
            specification.WorkerCount = 1;
            specification.BlockingWorkerCount = 1;
            m_JobSystem = Keire::CreateRef<Keire::JobSystem>(specification);
            m_OwnJobSystem = true;
        }
        m_JobScope = m_JobSystem->CreateScope("Shader Graph previews");
    }

    void ShaderGraphPanel::UpdatePreview(const Keire::ShaderGraphCompilation& compilation,
                                         const ShaderGraphPreviewSettings& settings)
    {
        m_PreviewProperties = compilation.Properties;
        m_PreviewSettings = settings;
        m_PreviewRefinement = false;
        m_PreviewDirty = true;
        if (++m_PreviewGeneration == 0)
            ++m_PreviewGeneration;
        m_PreviewCancellation->store(m_PreviewGeneration, std::memory_order_release);
    }

    void ShaderGraphPanel::ClearPreview() noexcept
    {
        m_PreviewImage.Reset();
        m_PreviewProperties.clear();
        m_PreviewSettings = {};
        m_PreviewRefinement = false;
        m_PreviewDirty = false;
        if (++m_PreviewGeneration == 0)
            ++m_PreviewGeneration;
        m_PreviewCancellation->store(m_PreviewGeneration, std::memory_order_release);
        m_AssetPicker.Clear();
        m_NodeAssetPicker.Clear();
    }
} // namespace KeireEditor
