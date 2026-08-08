#include "KeireClient/Editor/LightingPanel.h"

#include "KeireClient/Editor/SceneDocument.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <string>
#include <string_view>

namespace KeireEditor
{
    namespace
    {
        [[nodiscard]] int ResolutionPower(const std::uint32_t resolution) noexcept
        {
            int result = 0;
            for (auto value = resolution; value > 1U; value /= 2U)
                ++result;
            return result;
        }

        [[nodiscard]] std::string_view BackendName(const Keire::LightingBakeBackend value) noexcept
        {
            constexpr std::array names{"Automatic", "GPU", "CPU"};
            return names[static_cast<std::size_t>(value)];
        }

        [[nodiscard]] std::string_view QualityName(const Keire::LightingBakeQuality value) noexcept
        {
            constexpr std::array names{"Preview", "Medium", "High", "Production"};
            return names[static_cast<std::size_t>(value)];
        }
    } // namespace

    void LightingPanel::Attach(Keire::UiWorkspace& workspace)
    {
        m_Registration = workspace.RegisterPanel({"editor.lighting", "Lighting", false});
    }

    void LightingPanel::Draw(Keire::UiFrame& ui, const Keire::UiThemeDefinition& theme)
    {
        auto panel = ui.BeginPanel(m_Registration);
        if (!panel)
            return;
        ui.TextColored(theme.Accent, "LIGHTING");
        ui.Separator();
        const auto scene = m_Controller.LightingSceneDocument().EditingScene();
        if (!scene)
        {
            ui.TextColored(theme.MutedText, "Open a scene to configure and bake spatial lighting.");
            return;
        }
        auto settings = scene->LightingBakeConfiguration();
        bool changed = false;
        ui.Text("Backend: " + std::string(BackendName(settings.Backend)));
        if (ui.Button("Automatic###LightingBackendAutomatic"))
        {
            settings.Backend = Keire::LightingBakeBackend::Automatic;
            changed = true;
        }
        ui.SameLine();
        if (ui.Button("GPU###LightingBackendGpu"))
        {
            settings.Backend = Keire::LightingBakeBackend::GPU;
            changed = true;
        }
        ui.SameLine();
        if (ui.Button("CPU###LightingBackendCpu"))
        {
            settings.Backend = Keire::LightingBakeBackend::CPU;
            changed = true;
        }
        ui.Text("Quality: " + std::string(QualityName(settings.Quality)));
        for (std::size_t index = 0; index < 4U; ++index)
        {
            if (index != 0U)
                ui.SameLine();
            const auto quality = static_cast<Keire::LightingBakeQuality>(index);
            if (ui.Button(std::string(QualityName(quality)) + "###LightingQuality" + std::to_string(index)))
            {
                settings.Quality = quality;
                changed = true;
            }
        }
        int lightmapPower = ResolutionPower(settings.LightmapResolution);
        if (ui.SliderInt("Lightmap Resolution (2^n)", lightmapPower, 6, 14))
        {
            settings.LightmapResolution = 1U << lightmapPower;
            settings.MaximumLightmapResolution =
                std::max(settings.MaximumLightmapResolution, settings.LightmapResolution);
            changed = true;
        }
        int maximumPower = ResolutionPower(settings.MaximumLightmapResolution);
        if (ui.SliderInt("Maximum Resolution (2^n)", maximumPower, lightmapPower, 14))
        {
            settings.MaximumLightmapResolution = 1U << maximumPower;
            changed = true;
        }
        int texelsPerUnit = static_cast<int>(settings.TexelsPerUnit);
        if (ui.SliderInt("Texels Per Unit", texelsPerUnit, 1, 512))
        {
            settings.TexelsPerUnit = static_cast<std::uint32_t>(texelsPerUnit);
            changed = true;
        }
        int padding = static_cast<int>(settings.PaddingTexels);
        if (ui.SliderInt("Chart Padding", padding, 0, 32))
        {
            settings.PaddingTexels = static_cast<std::uint32_t>(padding);
            changed = true;
        }
        int bounces = static_cast<int>(settings.IndirectBounceCount);
        if (ui.SliderInt("Indirect Bounces", bounces, 0, 16))
        {
            settings.IndirectBounceCount = static_cast<std::uint32_t>(bounces);
            changed = true;
        }
        int samples = static_cast<int>(settings.SamplesPerTexel);
        if (ui.SliderInt("Samples Per Texel", samples, 1, 4096))
        {
            settings.SamplesPerTexel = static_cast<std::uint32_t>(samples);
            changed = true;
        }
        changed |= ui.Checkbox("Ambient Occlusion", settings.BakeAmbientOcclusion);
        changed |= ui.Checkbox("Denoise", settings.Denoise);
        if (changed)
        {
            try
            {
                scene->SetLightingBakeConfiguration(settings);
                m_Error.clear();
            }
            catch (const std::exception& error)
            {
                m_Error = error.what();
            }
        }

        ui.Spacing();
        ui.Separator();
        ui.Text("Bake Targets");
        ui.TextColored(theme.MutedText, std::to_string(scene->Query<Keire::ReflectionProbeComponent>().size()) +
                                            " reflection probe(s), " +
                                            std::to_string(scene->Query<Keire::LightProbeVolumeComponent>().size()) +
                                            " light-probe volume(s)");
        ui.TextColored(theme.MutedText, scene->BakedLighting() ? "Lighting set: " + scene->BakedLighting().ToString()
                                                               : "Lighting set: not baked");
        const auto busy = m_Controller.LightingBakeBusy();
        if (auto disabled = ui.BeginDisabled(busy || !m_Controller.LightingSceneDocument().Asset()); disabled)
        {
            if (ui.Button("Bake Lighting"))
            {
                try
                {
                    m_Controller.QueueLightingBake(false);
                    m_Error.clear();
                }
                catch (const std::exception& error)
                {
                    m_Error = error.what();
                }
            }
            ui.SameLine();
            if (ui.Button("Force Rebuild"))
            {
                try
                {
                    m_Controller.QueueLightingBake(true);
                    m_Error.clear();
                }
                catch (const std::exception& error)
                {
                    m_Error = error.what();
                }
            }
        }
        if (busy)
        {
            if (const auto progress = m_Controller.LightingBakeProgress())
            {
                const auto fraction = progress->Total == 0U ? 0.0F
                                                            : static_cast<float>(progress->Completed) /
                                                                  static_cast<float>(progress->Total);
                ui.ProgressBar(fraction, {0.0F, 18.0F}, progress->CurrentPath.string());
            }
            else
                ui.TextColored(theme.MutedText, "Lighting bake queued...");
        }
        if (!m_Error.empty())
            ui.TextColored(theme.Warning, m_Error);
    }
} // namespace KeireEditor
