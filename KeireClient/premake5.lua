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
    AddKeireManagedHostStaging()

    LinkSDL3()
