project(ProjectConfig.CLIENT_TARGET)
    location "."
    kind "ConsoleApp"

    ApplyCommonProjectSettings()

    pchheader "KeireClient/ClientPch.h"
    pchsource "Source/ClientPch.cpp"

    filter { "system:windows", "toolset:msc" }
        buildoptions { "/FIKeireClient/ClientPch.h" }

    filter { "system:linux or macosx" }
        buildoptions { "-include KeireClient/ClientPch.h" }

    -- Full editor symbols exceed the 4 KiB-page PDB limit. Retain symbols without incremental PDB growth.
    filter { "system:windows", "toolset:msc", "configurations:DevDist" }
        linkoptions { "/PDBPAGESIZE:16384", "/INCREMENTAL:NO" }

    filter {}

    files
    {
        "Include/**.h",
        "Include/**.hpp",
        "Source/**.c",
        "Source/**.cc",
        "Source/**.cpp",
        "Source/**.cxx"
    }
    files
    {
        "Source/Editor/VfxEffectGraphCanvas.cpp",
        "Source/Editor/VfxEffectGraphInspector.cpp",
        "Source/Editor/VfxEffectPanel.cpp",
        "Include/KeireClientInternal/Editor/VfxEffectPanelInternal.h"
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

    links { HubRuntimeTarget }

    LinkKeireSourceModules()
    LinkKeireCore()

    AddKeireManagedRuntimeDependency()
    AddKeireManagedHostStaging(true)

    LinkSDL3()

    if _ACTION ~= "ninja" then
        local postBuildPathPrefix = _ACTION == "gmake" and "KeireClient/" or ""
        local commandRepositoryRoot = _ACTION == "gmake" and "." or ".."

        filter { "configurations:Debug or DebugASan or DebugUBSan or DebugTSan or Coverage" }
            postbuildcommands
            {
                CopyFileIfChangedCommand(postBuildPathPrefix .. DependencyManifest.SodiumDebugRuntime,
                                         postBuildPathPrefix .. "%{cfg.targetdir}/" ..
                                             path.getname(DependencyManifest.SodiumDebugRuntime),
                                         commandRepositoryRoot)
            }

        filter { "configurations:Release or Profile or Dist or DevDist" }
            postbuildcommands
            {
                CopyFileIfChangedCommand(postBuildPathPrefix .. DependencyManifest.SodiumReleaseRuntime,
                                         postBuildPathPrefix .. "%{cfg.targetdir}/" ..
                                             path.getname(DependencyManifest.SodiumReleaseRuntime),
                                         commandRepositoryRoot)
            }
    end

    filter { "system:windows", "configurations:Dist or DevDist" }
        kind "WindowedApp"
        entrypoint "mainCRTStartup"

    filter {}
