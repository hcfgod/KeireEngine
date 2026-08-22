project(ProjectConfig.CLIENT_TARGET)
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
        "Source/**.cxx"
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

    dependson { AssetWorkerTarget, AssetToolTarget, RuntimeTarget }
    AddKeireManagedRuntimeDependency()
    AddKeireManagedHostStaging()

    LinkSDL3()

    if _ACTION ~= "ninja" then
        local postBuildPathPrefix = _ACTION == "gmake" and "KeireClient/" or ""

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

    filter { "system:windows", "configurations:Dist" }
        kind "WindowedApp"
        entrypoint "mainCRTStartup"

    filter {}
