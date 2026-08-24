OutputDir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"
DearImGuiProject = "DearImGui"
DearImGuiLibrary = ProjectConfig.PROJECT_NAMESPACE .. "ImGui"
ZstdProject = "Zstd"
ZstdLibrary = ProjectConfig.PROJECT_NAMESPACE .. "Zstd"
EnTTProject = "EnTT"
GLMProject = "GLM"
CoreArchiveTargets =
{
    ProjectConfig.CORE_TARGET,
    ProjectConfig.CORE_TARGET .. "Assets",
    ProjectConfig.CORE_TARGET .. "Build",
    ProjectConfig.CORE_TARGET .. "World",
    ProjectConfig.CORE_TARGET .. "Rendering",
    ProjectConfig.CORE_TARGET .. "Scenes",
    ProjectConfig.CORE_TARGET .. "Scripting",
    ProjectConfig.CORE_TARGET .. "Ui",
    ProjectConfig.CORE_TARGET .. "Vfx"
}

local function ConfigureNinjaArchiveReplacement()
    if _ACTION ~= "ninja" then
        return
    end

    local ninja = premake.modules.ninja
    local ninjaCpp = ninja and ninja.cpp
    if not ninjaCpp then
        error("Premake's Ninja C++ generator is unavailable.")
    end

    premake.override(ninjaCpp, "linkrule", function(base, cfg, toolset)
        toolset = toolset or ninja.gettoolset(cfg)
        if toolset == premake.tools.msc then
            base(cfg, toolset)
            return
        end

        -- GNU-family archivers update named members in-place and retain members removed from the current source graph.
        -- Delete the target immediately before reconstruction so archive membership matches the generated input list.
        local gettoolname = toolset.gettoolname
        toolset.gettoolname = function(toolConfig, toolName)
            local command = gettoolname(toolConfig, toolName)
            if toolName == "ar" then
                if cfg.system == "windows" then
                    return "if exist \"$out\" del /F /Q \"$out\" & if exist \"$out\" exit /B 1 & " .. command
                end
                return "rm -f $out && " .. command
            end
            return command
        end

        local succeeded, failure = pcall(base, cfg, toolset)
        toolset.gettoolname = gettoolname
        if not succeeded then
            error(failure, 0)
        end
    end)
end

ConfigureNinjaArchiveReplacement()

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

function CopyFileIfChangedCommand(source, destination, repositoryRoot)
    repositoryRoot = repositoryRoot or ".."
    if os.host() == "windows" then
        return 'powershell -NoProfile -ExecutionPolicy Bypass -File "' .. repositoryRoot ..
                   '/Scripts/Windows/copy-file-if-changed.ps1" -Source "' .. source .. '" -Destination "' ..
                   destination .. '"'
    end
    return 'bash "' .. repositoryRoot .. '/Scripts/Unix/copy-file-if-changed.sh" "' .. source .. '" "' ..
               destination .. '"'
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
    links(CoreArchiveTargets)
    links { DearImGuiProject, ZstdProject }

    -- Core is split into domain archives so an isolated edit does not rewrite a monolithic library. GNU-family
    -- linkers need archive grouping because subsystem references intentionally cross those internal boundaries.
    filter { "system:linux or macosx", "toolset:gcc or clang" }
        linkgroups "On"

    filter { "system:windows", "toolset:gcc" }
        linkgroups "On"

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
                CopyFileIfChangedCommand(netHostRuntime, runtimeDirectory .. "/nethost.dll", commandRepositoryRoot)
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
    objdir (repositoryRoot .. "/Build/Intermediates/" .. IntermediateOutputDir .. "/%{prj.name}")
    debugdir (repositoryRoot)

    -- Premake's Ninja backend prefixes a nested project's location twice when it resolves that project's PCH
    -- input. Keep the forced include below each PCH-owning project, but compile it as a normal header on Unix
    -- Ninja builds so the generated graph never refers to paths such as KeireCore/KeireCore/Include/....
    filter { "action:ninja", "system:linux or macosx" }
        enablepch "Off"

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

    -- Premake's Ninja backend does not translate systemversion into Clang's macOS deployment-target option. Keep
    -- first-party objects and final links aligned with the dependency lock instead of inheriting the host SDK default.
    filter { "system:macosx", "toolset:gcc or clang" }
        buildoptions { "-mmacosx-version-min=" .. DependencyManifest.MacOSDeploymentTarget }
        linkoptions { "-mmacosx-version-min=" .. DependencyManifest.MacOSDeploymentTarget }

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
