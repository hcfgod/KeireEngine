project(RuntimeTarget)
    location "."
    kind "ConsoleApp"

    ApplyCommonProjectSettings()

    files
    {
        "Include/**.h",
        "Include/**.hpp",
        "Source/**.cpp"
    }
    AddKeireApplicationIcon()

    includedirs
    {
        "Include",
        "../" .. ProjectConfig.CORE_DIRECTORY .. "/Include"
    }

    externalincludedirs
    {
        VendorIncludeDirs.json
    }

    LinkKeireSourceModules()
    LinkKeireCore()
    LinkSDL3()
    AddKeireManagedRuntimeDependency()
    AddKeireManagedHostStaging()

    filter { "system:windows", "configurations:Dist" }
        kind "WindowedApp"
        entrypoint "mainCRTStartup"

    filter {}
