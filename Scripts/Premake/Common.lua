OutputDir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"
DearImGuiProject = "DearImGui"
DearImGuiLibrary = ProjectConfig.PROJECT_NAMESPACE .. "ImGui"
ZstdProject = "Zstd"
ZstdLibrary = ProjectConfig.PROJECT_NAMESPACE .. "Zstd"
EnTTProject = "EnTT"
GLMProject = "GLM"

VendorIncludeDirs = {
    spdlog = "../Vendor/spdlog/include",
    doctest = "../Vendor/doctest",
    json = "../Vendor/json/include",
    imgui = "../Vendor/imgui",
    imguiBackends = "../Vendor/imgui/backends",
    imguiMisc = "../Vendor/imgui/misc/cpp",
    zstd = "../Vendor/zstd/lib",
    entt = "../Vendor/entt/src",
    glm = "../Vendor/glm"
}

function LinkKeireCore()
    links
    {
        ProjectConfig.CORE_TARGET,
        DearImGuiProject,
        ZstdProject
    }
end

function LinkSDL3()
    externalincludedirs { DependencyManifest.SDL3Include }
    links { DependencyManifest.SDL3PlatformLinks }

    filter { "configurations:Debug or DebugASan or DebugUBSan or DebugTSan or Coverage" }
        links { DependencyManifest.SDL3DebugLibrary }

    filter { "configurations:Release or Dist" }
        links { DependencyManifest.SDL3ReleaseLibrary }

    filter {}
end

function ApplyCommonProjectSettings(repositoryRoot)
    repositoryRoot = repositoryRoot or ".."
    language "C++"
    cppdialect "C++20"
    staticruntime "off"
    warnings "Extra"
    defines { "KEIRE_STATIC", 'KEIRE_BUILD_CONFIGURATION="%{cfg.buildcfg}"', 'KEIRE_BUILD_ARCHITECTURE="%{cfg.architecture}"' }

    targetdir (repositoryRoot .. "/Build/Bin/" .. OutputDir .. "/%{prj.name}")
    objdir (repositoryRoot .. "/Build/Intermediates/" .. OutputDir .. "/%{prj.name}")
    debugdir (repositoryRoot)

    filter "options:ci"
        fatalwarnings "All"

    filter {}

    local selectedToolset = SelectedToolset
    local usesMsvcCommandLine = os.host() == "windows" and
        ((_ACTION and _ACTION:match("^vs")) or (_ACTION == "ninja" and (selectedToolset == "default" or selectedToolset == "msc")))
    if usesMsvcCommandLine then
        buildoptions { "/utf-8", "/permissive-", "/Zc:__cplusplus", "/Zc:preprocessor", "/MP" }
    end

    filter { "toolset:gcc or clang" }
        buildoptions { "-Wpedantic", "-Wconversion", "-Wshadow" }

    filter { "configurations:Debug or DebugASan or DebugUBSan or DebugTSan" }
        runtime "Debug"
        symbols "on"
        defines
        {
            "KEIRE_LOG_DEFAULT_TRACE",
            "SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE"
            , "KEIRE_ASSERTIONS_ENABLED"
        }

    filter "configurations:Release"
        runtime "Release"
        optimize "on"
        symbols "on"
        defines
        {
            "NDEBUG",
            "SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO"
        }

    filter "configurations:Dist"
        runtime "Release"
        optimize "full"
        symbols "off"
        linktimeoptimization "On"
        defines
        {
            "NDEBUG",
            "SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO"
        }

    filter "configurations:Coverage"
        runtime "Debug"
        symbols "on"
        defines {
            "KEIRE_LOG_DEFAULT_TRACE",
            "SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE"
        }

    ApplySanitizerSettings()

    filter {}
end

function ApplySanitizerSettings()
    filter { "configurations:DebugASan", "toolset:msc" }
        editandcontinue "Off"
        buildoptions { "/fsanitize=address" }

    filter { "configurations:DebugASan", "toolset:msc", "kind:ConsoleApp" }
        linkoptions { "/INCREMENTAL:NO" }

    filter { "configurations:DebugASan", "toolset:gcc or clang" }
        buildoptions { "-fsanitize=address", "-fno-omit-frame-pointer" }
        linkoptions { "-fsanitize=address" }

    filter { "configurations:DebugUBSan", "toolset:gcc or clang" }
        buildoptions { "-fsanitize=undefined", "-fno-omit-frame-pointer" }
        linkoptions { "-fsanitize=undefined" }

    filter { "configurations:DebugTSan", "toolset:gcc or clang" }
        buildoptions { "-fsanitize=thread", "-fno-omit-frame-pointer" }
        linkoptions { "-fsanitize=thread" }

    filter { "configurations:Coverage", "toolset:clang" }
        buildoptions { "-fprofile-instr-generate", "-fcoverage-mapping", "-fno-omit-frame-pointer" }
        linkoptions { "-fprofile-instr-generate", "-fcoverage-mapping" }

    filter {}
end
