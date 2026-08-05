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
        "../" .. ProjectConfig.CORE_DIRECTORY .. "/Include"
    }

    LinkKeireSourceModules()
    LinkKeireCore()

    dependson { AssetWorkerTarget, AssetToolTarget, RuntimeTarget }
    AddKeireManagedRuntimeDependency()

    local clientCommandPrefix = _ACTION == "ninja" and "KeireClient/" or ""

    postbuildcommands
    {
        '{MKDIR} "' .. clientCommandPrefix .. '%{cfg.targetdir}/Managed"',
        '{COPYFILE} "' .. clientCommandPrefix .. '../Build/Managed/Keire.Managed.dll" "' ..
            clientCommandPrefix .. '%{cfg.targetdir}/Managed/Keire.Managed.dll"'
    }

    LinkSDL3()
