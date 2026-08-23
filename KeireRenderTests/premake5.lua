project(ProjectConfig.PROJECT_NAMESPACE .. "RenderTests")
    location "."
    kind "ConsoleApp"

    ApplyCommonProjectSettings()

    files
    {
        "Include/**.h",
        "Source/**.cpp"
    }

    includedirs
    {
        "Include",
        "../" .. ProjectConfig.CORE_DIRECTORY .. "/Include"
    }

    externalincludedirs
    {
        VendorIncludeDirs.doctest
    }

    LinkKeireCore()
    LinkSDL3()
