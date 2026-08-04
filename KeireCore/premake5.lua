project(ProjectConfig.CORE_TARGET)
    location "."
    kind "StaticLib"

    ApplyCommonProjectSettings()

    files
    {
        "Include/**.h",
        "Include/**.hpp",
        "Source/**.c",
        "Source/**.cc",
        "Source/**.cpp",
        "Source/**.cxx",
        "Source/ECS/Components/CameraComponent.cpp",
        "Source/ECS/Components/DirectionalLightComponent.cpp",
        "Source/ECS/Components/MeshRendererComponent.cpp",
        "Source/ECS/Components/TransformComponent.cpp"
    }

    includedirs
    {
        "Include",
        "../Build/Generated"
    }

    defines { "KEIRE_BUILDING_LIBRARY", "DT_POLYREF64" }

    local commandRepositoryRoot = (_ACTION == "ninja" or _ACTION == "gmake") and "." or ".."
    local windowsScripts = commandRepositoryRoot .. "/Scripts/Windows"
    local unixScripts = commandRepositoryRoot .. "/Scripts/Unix"
    local windowsPrebuildCommands = {
        "powershell -NoProfile -ExecutionPolicy Bypass -File " .. windowsScripts .. "/build-info.ps1",
        "powershell -NoProfile -ExecutionPolicy Bypass -File " .. windowsScripts .. "/builtin-shaders.ps1",
        "powershell -NoProfile -ExecutionPolicy Bypass -File " .. windowsScripts .. "/builtin-skinning.ps1",
        "powershell -NoProfile -ExecutionPolicy Bypass -File " .. windowsScripts .. "/builtin-vfx.ps1"
    }
    if _ACTION == "ninja" then
        local prebuildDirectory = commandRepositoryRoot .. "/Build/Intermediates/" .. OutputDir .. "/" ..
                                      ProjectConfig.CORE_TARGET
        table.insert(windowsPrebuildCommands,
                     "powershell -NoProfile -ExecutionPolicy Bypass -File " .. windowsScripts ..
                         "/touch-ninja-stamp.ps1 -Path " .. prebuildDirectory .. "/" ..
                         ProjectConfig.CORE_TARGET .. ".prebuild && exit /b 0")
    end

    filter "system:windows"
        prebuildcommands(windowsPrebuildCommands)

    filter { "system:linux or macosx" }
        prebuildcommands {
            "bash " .. unixScripts .. "/build-info.sh",
            "bash " .. unixScripts .. "/builtin-shaders.sh",
            "bash " .. unixScripts .. "/builtin-skinning.sh",
            "bash " .. unixScripts .. "/builtin-vfx.sh"
        }

    filter {}

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

    links { DearImGuiProject, ZstdProject }

    if _ACTION and _ACTION:match("^vs") then
        dependson { EnTTProject, GLMProject }
    end
