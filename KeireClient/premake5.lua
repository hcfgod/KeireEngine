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

    includedirs
    {
        "Include",
        "../" .. ProjectConfig.CORE_DIRECTORY .. "/Include"
    }

    LinkKeireCore()

    dependson { AssetWorkerTarget, KeireManagedProject }

    postbuildcommands
    {
        '{MKDIR} "%{cfg.targetdir}/Managed"',
        '{COPYFILE} "../Build/Managed/Keire.Managed.dll" "%{cfg.targetdir}/Managed/Keire.Managed.dll"'
    }

    LinkSDL3()
