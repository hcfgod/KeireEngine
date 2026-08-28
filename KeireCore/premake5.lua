local CoreGeneratedContentTarget = ProjectConfig.CORE_TARGET .. "GeneratedContent"

local function AddGeneratedContentCommands()
    local commandRepositoryRoot = (_ACTION == "ninja" or _ACTION == "gmake") and "." or ".."
    local windowsScripts = commandRepositoryRoot .. "/Scripts/Windows"
    local unixScripts = commandRepositoryRoot .. "/Scripts/Unix"
    local windowsPrebuildCommands = {
        "powershell -NoProfile -ExecutionPolicy Bypass -File " .. windowsScripts .. "/prepare-generated-content.ps1"
    }
    if _ACTION == "ninja" then
        local prebuildDirectory = commandRepositoryRoot .. "/Build/Intermediates/" .. IntermediateOutputDir .. "/" ..
                                      CoreGeneratedContentTarget
        table.insert(windowsPrebuildCommands,
                     "powershell -NoProfile -ExecutionPolicy Bypass -File " .. windowsScripts ..
                         "/touch-ninja-stamp.ps1 -Path " .. prebuildDirectory .. "/" ..
                         CoreGeneratedContentTarget .. ".prebuild && exit /b 0")
    end

    filter "system:windows"
        prebuildcommands(windowsPrebuildCommands)

    filter { "system:linux or macosx" }
        prebuildcommands {
            "bash " .. unixScripts .. "/build-info.sh",
            "bash " .. unixScripts .. "/builtin-shaders.sh",
            "bash " .. unixScripts .. "/builtin-skinning.sh",
            "bash " .. unixScripts .. "/builtin-vfx.sh",
            "bash " .. unixScripts .. "/builtin-occlusion.sh",
            "bash " .. unixScripts .. "/builtin-spatial-selection.sh"
        }

    filter {}
end

local function ConfigureCoreArchive(target, pchSource, sourceFiles, macSources, usesObjectiveCArc)
    project(target)
        location "."
        kind "StaticLib"

        ApplyCommonProjectSettings()

        -- MSBuild already schedules these archives concurrently. Disable the global per-project /MP setting so /m
        -- does not multiply project parallelism and exhaust memory on developer workstations. Using /MP1 here looks
        -- equivalent, but MSBuild omits it from PCH tracking logs and then invalidates the PCH on every build.
        filter { "system:windows", "toolset:msc" }
            removebuildoptions { "/MP" }

        filter {}

        pchheader "KeireInternal/KeireCorePch.h"
        pchsource(pchSource)

        filter { "system:windows", "toolset:msc" }
            buildoptions { "/FIKeireInternal/KeireCorePch.h" }

        filter { "system:linux or macosx" }
            buildoptions { "-include KeireInternal/KeireCorePch.h" }

        filter {}

        files(sourceFiles)
        files { pchSource }

        includedirs
        {
            "Include",
            "../Build/Generated"
        }

        defines { "KEIRE_BUILDING_LIBRARY", "DT_POLYREF64" }
        dependson { CoreGeneratedContentTarget }

        if macSources ~= nil then
            filter "system:macosx"
                files(macSources)

            filter { "files:**.mm" }
                enablepch "Off"

            if usesObjectiveCArc then
                filter { "system:macosx", "files:**.mm" }
                    buildoptions { "-fobjc-arc" }
            end

            filter {}
        end

        externalincludedirs
        {
            VendorIncludeDirs.spdlog,
            VendorIncludeDirs.json,
            VendorIncludeDirs.imgui,
            VendorIncludeDirs.imguiBackends,
            VendorIncludeDirs.imguiMisc,
            VendorIncludeDirs.zstd,
            VendorIncludeDirs.entt,
            VendorIncludeDirs.glm,
            VendorIncludeDirs.stb,
            DependencyManifest.AssimpInclude,
            DependencyManifest.JoltInclude,
            DependencyManifest.RecastInclude,
            DependencyManifest.MiniaudioInclude,
            DependencyManifest.CoralInclude,
            DependencyManifest.SDL3Include
        }

        filter "configurations:Profile"
            externalincludedirs { "../Build/Dependencies/tracy/public" }

        filter {}
end

project(CoreGeneratedContentTarget)
    location "."
    kind "StaticLib"

    ApplyCommonProjectSettings()
    files { "../Scripts/Premake/Source/CoreGeneratedContentAnchor.cpp" }
    targetname(ProjectConfig.CORE_TARGET .. "GeneratedContentProxy")
    AddGeneratedContentCommands()

ConfigureCoreArchive(ProjectConfig.CORE_TARGET, "Source/KeireCorePch.cpp",
                     {
                         "Include/**.h",
                         "Include/**.hpp",
                         "Source/*.c",
                         "Source/*.cc",
                         "Source/*.cpp",
                         "Source/*.cxx",
                         "Source/Diagnostics/**.c",
                         "Source/Diagnostics/**.cc",
                         "Source/Diagnostics/**.cpp",
                         "Source/Diagnostics/**.cxx",
                         "Source/Input/**.c",
                         "Source/Input/**.cc",
                         "Source/Input/**.cpp",
                         "Source/Input/**.cxx",
                         "Source/Jobs/**.c",
                         "Source/Jobs/**.cc",
                         "Source/Jobs/**.cpp",
                         "Source/Jobs/**.cxx",
                         "Source/Math/**.c",
                         "Source/Math/**.cc",
                         "Source/Math/**.cpp",
                         "Source/Math/**.cxx",
                         "Source/Memory/**.c",
                         "Source/Memory/**.cc",
                         "Source/Memory/**.cpp",
                         "Source/Memory/**.cxx",
                         "Source/Modules/**.c",
                         "Source/Modules/**.cc",
                         "Source/Modules/**.cpp",
                         "Source/Modules/**.cxx",
                         "Source/ThirdParty/**.c",
                         "Source/ThirdParty/**.cc",
                         "Source/ThirdParty/**.cpp",
                         "Source/ThirdParty/**.cxx"
                     })
removefiles
{
    "Source/EditorCameraController.cpp",
    "Source/FolderDialog.cpp",
    "Source/SystemTrayIcon.cpp",
    "Source/Ui*.cpp",
    "Source/WindowAppearance.cpp",
    "Source/WindowChrome*.cpp"
}
links { DearImGuiProject, ZstdProject }

filter "files:Source/Diagnostics/TracyClientIntegration.cpp"
    enablepch "Off"
    warnings "Off"

filter {}

ConfigureCoreArchive(ProjectConfig.CORE_TARGET .. "Assets", "Source/Pch/KeireCoreAssetsPch.cpp",
                     {
                         "Source/Assets/**.c",
                         "Source/Assets/**.cc",
                         "Source/Assets/**.cpp",
                         "Source/Assets/**.cxx",
                         "Source/Audio/**.c",
                         "Source/Audio/**.cc",
                         "Source/Audio/**.cpp",
                         "Source/Audio/**.cxx",
                         "Source/Authoring/**.c",
                         "Source/Authoring/**.cc",
                         "Source/Authoring/**.cpp",
                         "Source/Authoring/**.cxx"
                     })

ConfigureCoreArchive(ProjectConfig.CORE_TARGET .. "Build", "Source/Pch/KeireCoreBuildPch.cpp",
                     {
                         "Source/Build/**.c",
                         "Source/Build/**.cc",
                         "Source/Build/**.cpp",
                         "Source/Build/**.cxx",
                         "Source/Project/**.c",
                         "Source/Project/**.cc",
                         "Source/Project/**.cpp",
                         "Source/Project/**.cxx"
                     },
                     { "Source/Build/PlayerSupportDownloadMac.mm" })

ConfigureCoreArchive(ProjectConfig.CORE_TARGET .. "World", "Source/Pch/KeireCoreWorldPch.cpp",
                     {
                         "Source/Animation/**.c",
                         "Source/Animation/**.cc",
                         "Source/Animation/**.cpp",
                         "Source/Animation/**.cxx",
                         "Source/ECS/**.c",
                         "Source/ECS/**.cc",
                         "Source/ECS/**.cpp",
                         "Source/ECS/**.cxx",
                         "Source/ECS/Components/CameraComponent.cpp",
                         "Source/ECS/Components/DirectionalLightComponent.cpp",
                         "Source/ECS/Components/MeshRendererComponent.cpp",
                         "Source/ECS/Components/TransformComponent.cpp",
                         "Source/Navigation/**.c",
                         "Source/Navigation/**.cc",
                         "Source/Navigation/**.cpp",
                         "Source/Navigation/**.cxx",
                         "Source/Physics/**.c",
                         "Source/Physics/**.cc",
                         "Source/Physics/**.cpp",
                         "Source/Physics/**.cxx",
                         "Source/Replay/**.c",
                         "Source/Replay/**.cc",
                         "Source/Replay/**.cpp",
                         "Source/Replay/**.cxx",
                         "Source/Streaming/**.c",
                         "Source/Streaming/**.cc",
                         "Source/Streaming/**.cpp",
                         "Source/Streaming/**.cxx"
                     })

ConfigureCoreArchive(ProjectConfig.CORE_TARGET .. "Rendering", "Source/Pch/KeireCoreRenderingPch.cpp",
                     {
                         "Source/Rendering/**.c",
                         "Source/Rendering/**.cc",
                         "Source/Rendering/**.cpp",
                         "Source/Rendering/**.cxx"
                     })

ConfigureCoreArchive(ProjectConfig.CORE_TARGET .. "Scenes", "Source/Pch/KeireCoreScenesPch.cpp",
                     {
                         "Source/Scenes/**.c",
                         "Source/Scenes/**.cc",
                         "Source/Scenes/**.cpp",
                         "Source/Scenes/**.cxx"
                     })

ConfigureCoreArchive(ProjectConfig.CORE_TARGET .. "Scripting", "Source/Pch/KeireCoreScriptingPch.cpp",
                     {
                         "Source/Scripting/**.c",
                         "Source/Scripting/**.cc",
                         "Source/Scripting/**.cpp",
                         "Source/Scripting/**.cxx"
                     })

ConfigureCoreArchive(ProjectConfig.CORE_TARGET .. "Ui", "Source/Pch/KeireCoreUiPch.cpp",
                     {
                         "Source/Ui/**.c",
                         "Source/Ui/**.cc",
                         "Source/Ui/**.cpp",
                         "Source/Ui/**.cxx",
                         "Source/EditorCameraController.cpp",
                         "Source/FolderDialog.cpp",
                         "Source/SystemTrayIcon.cpp",
                         "Source/Ui*.cpp",
                         "Source/WindowAppearance.cpp",
                         "Source/WindowChrome*.cpp"
                     },
                     { "Source/WindowChromeMac.mm" }, true)

-- The VFX asset codec, graph, lowering, validation, and compilation stages share a private compiler contract and must
-- remain in the same archive. Keep recursive discovery so adding a focused stage cannot leave the generated graph
-- linked against only the public VfxAssets facade.
ConfigureCoreArchive(ProjectConfig.CORE_TARGET .. "Vfx", "Source/Pch/KeireCoreVfxPch.cpp",
                     {
                         "Include/KeireInternal/Vfx/VfxWorldInternal.h",
                         "Source/Vfx/VfxExpressionEvaluation.cpp",
                         "Source/Vfx/VfxExpressions.cpp",
                         "Source/Vfx/VfxSystem.cpp",
                         "Source/Vfx/VfxWorldProgram.cpp",
                         "Source/Vfx/VfxWorldSimulation.cpp",
                         "Source/Vfx/VfxWorldSnapshots.cpp",
                         "Source/Vfx/**.c",
                         "Source/Vfx/**.cc",
                         "Source/Vfx/**.cpp",
                         "Source/Vfx/**.cxx"
                     })
