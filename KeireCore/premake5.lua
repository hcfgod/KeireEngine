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

    filter "system:windows"
        prebuildcommands {
            "if exist Scripts\\Windows\\build-info.ps1 (powershell -NoProfile -ExecutionPolicy Bypass -File Scripts\\Windows\\build-info.ps1 && powershell -NoProfile -ExecutionPolicy Bypass -File Scripts\\Windows\\builtin-shaders.ps1) else (powershell -NoProfile -ExecutionPolicy Bypass -File ..\\Scripts\\Windows\\build-info.ps1 && powershell -NoProfile -ExecutionPolicy Bypass -File ..\\Scripts\\Windows\\builtin-shaders.ps1)"
        }

    filter { "system:linux or macosx" }
        prebuildcommands {
            "if [ -f Scripts/Unix/build-info.sh ]; then bash Scripts/Unix/build-info.sh; else bash ../Scripts/Unix/build-info.sh; fi",
            "if [ -f Scripts/Unix/builtin-shaders.sh ]; then bash Scripts/Unix/builtin-shaders.sh; else bash ../Scripts/Unix/builtin-shaders.sh; fi"
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
