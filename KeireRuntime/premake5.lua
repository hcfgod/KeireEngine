project(RuntimeTarget)
    location "."
    kind "ConsoleApp"

    ApplyCommonProjectSettings()

    files
    {
        "Source/**.cpp"
    }
    AddKeireApplicationIcon()

    includedirs
    {
        "../" .. ProjectConfig.CORE_DIRECTORY .. "/Include"
    }

    externalincludedirs
    {
        VendorIncludeDirs.json
    }

    LinkKeireSourceModules()
    LinkKeireCore()
    LinkSDL3()

    filter { "system:windows", "configurations:Dist" }
        kind "WindowedApp"
        entrypoint "mainCRTStartup"

    filter {}
