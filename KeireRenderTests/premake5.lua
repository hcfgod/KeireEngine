project(ProjectConfig.PROJECT_NAMESPACE .. "RenderTests")
    location "."
    kind "ConsoleApp"

    ApplyCommonProjectSettings()

    files
    {
        "Source/**.cpp"
    }

    includedirs
    {
        "../" .. ProjectConfig.CORE_DIRECTORY .. "/Include"
    }

    externalincludedirs
    {
        VendorIncludeDirs.doctest,
        VendorIncludeDirs.spdlog
    }

    LinkKeireCore()
    LinkSDL3()
