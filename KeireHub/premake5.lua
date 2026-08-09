project(ProjectConfig.HUB_TARGET)
    location "."
    kind "ConsoleApp"

    ApplyCommonProjectSettings()

    files
    {
        "Include/**.h",
        "Include/**.hpp",
        "Source/**.c",
        "Source/**.cc",
        "Source/**.cpp",
        "Source/**.cxx",
        "Source/**.h"
    }
    AddKeireApplicationIcon()

    includedirs
    {
        "Include",
        "../KeireHubRuntime/Include",
        "../" .. ProjectConfig.CORE_DIRECTORY .. "/Include"
    }

    externalincludedirs
    {
        VendorIncludeDirs.json
    }

    defines
    {
        "KEIRE_EDITOR_TARGET=\"" .. ProjectConfig.CLIENT_TARGET .. "\"",
        "KEIRE_HUB_WORKER_TARGET=\"" .. HubWorkerTarget .. "\""
    }

    dependson { HubWorkerTarget }

    links { HubRuntimeTarget }

    LinkKeireHubNativeHttp()

    LinkKeireSourceModules()
    LinkKeireCore()

    LinkSDL3()
    ApplyLargeWindowsStack()

    filter "system:windows"
        links { "Bcrypt", "Wintrust" }

    filter { "system:windows", "configurations:Dist" }
        kind "WindowedApp"
        entrypoint "mainCRTStartup"

    if _ACTION ~= "ninja" then
        local postBuildPathPrefix = _ACTION == "gmake" and "KeireHub/" or ""

        filter { "configurations:Debug or DebugASan or DebugUBSan or DebugTSan or Coverage" }
            postbuildcommands
            {
                '{COPYFILE} "' .. postBuildPathPrefix .. DependencyManifest.SodiumDebugRuntime .. '" "' ..
                    postBuildPathPrefix .. '%{cfg.targetdir}/' ..
                    path.getname(DependencyManifest.SodiumDebugRuntime) .. '"'
            }

        filter { "configurations:Release or Dist" }
            postbuildcommands
            {
                '{COPYFILE} "' .. postBuildPathPrefix .. DependencyManifest.SodiumReleaseRuntime .. '" "' ..
                    postBuildPathPrefix .. '%{cfg.targetdir}/' ..
                    path.getname(DependencyManifest.SodiumReleaseRuntime) .. '"'
            }
    end

    filter {}
