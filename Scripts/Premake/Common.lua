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
    glm = "../Vendor/glm",
    stb = "../Vendor/stb"
}

function GeneratorRootPath(path)
    if (_ACTION == "ninja" or _ACTION == "gmake") and SelectedToolset ~= "msc" then
        local rootRelativePath = path:gsub("^%.%./", "")
        return rootRelativePath
    end
    return path
end

local function DependencyLink(path)
    local resolved = GeneratorRootPath(path)
    if (_ACTION == "ninja" or _ACTION == "gmake") and SelectedToolset ~= "msc" then
        local directory, library = resolved:match("^(.*)/(lib[^/]+%.a)$")
        if directory ~= nil then
            libdirs { directory }
            return ":" .. library
        end
    end
    return resolved
end

local function DependencyLinks(paths)
    local result = {}
    for _, path in ipairs(paths) do
        table.insert(result, DependencyLink(path))
    end
    return result
end

function LinkKeireCore()
    links
    {
        ProjectConfig.CORE_TARGET,
        DearImGuiProject,
        ZstdProject
    }

    filter { "configurations:Debug or DebugASan or DebugUBSan or DebugTSan or Coverage" }
        links
        {
            DependencyLink(DependencyManifest.AssimpDebugLibrary),
            DependencyLink(DependencyManifest.AssimpZlibDebugLibrary),
            DependencyLink(DependencyManifest.JoltDebugLibrary),
            DependencyLink(DependencyManifest.MiniaudioDebugLibrary),
            DependencyLink(DependencyManifest.CoralDebugLibrary)
        }
        links(DependencyLinks(DependencyManifest.RecastDebugLibraries))

    filter { "configurations:Release or Dist" }
        links
        {
            DependencyLink(DependencyManifest.AssimpReleaseLibrary),
            DependencyLink(DependencyManifest.AssimpZlibReleaseLibrary),
            DependencyLink(DependencyManifest.JoltReleaseLibrary),
            DependencyLink(DependencyManifest.MiniaudioReleaseLibrary),
            DependencyLink(DependencyManifest.CoralReleaseLibrary)
        }
        links(DependencyLinks(DependencyManifest.RecastReleaseLibraries))

    filter {}
        links { DependencyLink(DependencyManifest.CoralNetHostLibrary) }

    filter "system:windows"
        links { "crypt32", "winhttp" }

    filter "system:linux"
        links { "curl" }

    filter "system:macosx"
        links { "Foundation.framework" }

    filter {}

    if _ACTION ~= "ninja" then
        -- Windows loads nethost before main executes, so every final executable that links KeireCore must be runnable
        -- without depending on a repository launcher to copy the DLL. Ninja remains launcher-staged because Premake's
        -- generated Ninja post-build stamp is not compatible with cmd.exe.
        local commandRepositoryRoot = _ACTION == "gmake" and "." or ".."
        local netHostRuntime = DependencyManifest.CoralNetHostRuntime
        if _ACTION == "gmake" then
            netHostRuntime = netHostRuntime:gsub("^%.%./", "")
        end
        local runtimeDirectory = commandRepositoryRoot .. "/Build/Bin/" .. OutputDir .. "/%{prj.name}"

        filter "system:windows"
            postbuildcommands
            {
                '{COPYFILE} "' .. netHostRuntime .. '" "' .. runtimeDirectory .. '/nethost.dll"'
            }

        filter {}
    end
end

function LinkKeireHubNativeHttp()
    filter "system:windows"
        links { "winhttp" }

    filter "system:linux"
        links { "curl" }

    filter "system:macosx"
        links
        {
            "CFNetwork.framework",
            "Foundation.framework",
            "Security.framework"
        }

    filter {}
end

function LinkKeireSourceModules()
    includedirs { "../SourceModules/Include" }
    links { ProjectConfig.PROJECT_NAMESPACE .. "SourceModules" }
end

function LinkSDL3()
    externalincludedirs { DependencyManifest.SDL3Include }
    links { DependencyManifest.SDL3PlatformLinks }

    filter { "configurations:Debug or DebugASan or DebugUBSan or DebugTSan or Coverage" }
        links { DependencyLink(DependencyManifest.SDL3DebugLibrary) }

    filter { "configurations:Release or Dist" }
        links { DependencyLink(DependencyManifest.SDL3ReleaseLibrary) }

    filter {}
end

function AddKeireApplicationIcon()
    local iconResource = _ACTION == "ninja" and "Config/Branding/Keire.res" or "../Config/Branding/Keire.res"
    filter "system:windows"
        linkoptions { '"' .. iconResource .. '"' }

    filter {}
end

function ApplyLargeWindowsStack()
    filter { "system:windows", "toolset:msc" }
        linkoptions { "/STACK:8388608" }

    filter { "system:windows", "toolset:clang" }
        linkoptions { "-Xlinker", "/STACK:8388608" }

    filter { "system:windows", "toolset:gcc" }
        linkoptions { "-Wl,--stack,8388608" }

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
        externalanglebrackets "On"
        externalwarnings "Off"
        buildoptions
        {
            "/utf-8",
            "/permissive-",
            "/Zc:__cplusplus",
            "/Zc:preprocessor",
            "/MP"
        }
    end

    filter { "toolset:gcc or clang" }
        buildoptions { "-Wpedantic", "-Wconversion", "-Wshadow" }

    -- Partial C++20 aggregate initialization intentionally value-initializes fields that retain their defaults.
    filter { "toolset:gcc or clang" }
        buildoptions { "-Wno-missing-field-initializers" }

    filter { "system:windows", "toolset:clang", "configurations:Debug or DebugASan or DebugUBSan or DebugTSan or Coverage" }
        buildoptions { "-fms-runtime-lib=dll_dbg" }
        linkoptions { "-fms-runtime-lib=dll_dbg" }

    filter { "system:windows", "toolset:clang", "configurations:Release or Dist" }
        buildoptions { "-fms-runtime-lib=dll" }
        linkoptions { "-fms-runtime-lib=dll" }

    filter { "configurations:Debug or DebugASan or DebugUBSan or DebugTSan" }
        runtime "Debug"
        symbols "on"
        defines
        {
            "KEIRE_LOG_DEFAULT_TRACE",
            "KEIRE_COMPILED_LOG_LEVEL=0"
            , "KEIRE_ASSERTIONS_ENABLED"
        }

    filter "configurations:Release"
        runtime "Release"
        optimize "on"
        symbols "on"
        defines
        {
            "NDEBUG",
            "KEIRE_COMPILED_LOG_LEVEL=2"
        }

    filter "configurations:Dist"
        runtime "Release"
        optimize "full"
        symbols "off"
        linktimeoptimization "On"
        defines
        {
            "NDEBUG",
            "KEIRE_DISTRIBUTION",
            "KEIRE_COMPILED_LOG_LEVEL=2"
        }

    filter "configurations:Coverage"
        runtime "Debug"
        symbols "on"
        defines {
            "KEIRE_LOG_DEFAULT_TRACE",
            "KEIRE_COMPILED_LOG_LEVEL=0"
        }

    ApplySanitizerSettings()

    filter {}
end

function ApplySanitizerSettings()
    filter { "configurations:DebugASan", "toolset:msc" }
        editandcontinue "Off"
        -- CMake-built private archives use the standard unannotated MSVC STL ABI. Keep that ABI consistent while
        -- retaining AddressSanitizer instrumentation for every first-party translation unit.
        buildoptions { "/D_DISABLE_STRING_ANNOTATION", "/D_DISABLE_VECTOR_ANNOTATION", "/fsanitize=address" }

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

    -- GCC reports that atomic_thread_fence is not modeled by TSan. Preserve the diagnostic without promoting this
    -- instrumentation limitation to a compile error; the sanitizer remains enabled for the complete target.
    filter { "configurations:DebugTSan", "toolset:gcc" }
        buildoptions { "-Wno-error=tsan" }

    filter { "configurations:Coverage", "toolset:clang" }
        buildoptions { "-fprofile-instr-generate", "-fcoverage-mapping", "-fno-omit-frame-pointer" }
        linkoptions { "-fprofile-instr-generate", "-fcoverage-mapping" }

    filter {}
end
