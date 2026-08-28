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
        "../" .. ProjectConfig.CORE_DIRECTORY .. "/Include",
        "../Build/Generated"
    }

    externalincludedirs
    {
        VendorIncludeDirs.doctest
    }

    LinkKeireCore()
    LinkSDL3()
    dependson { ProjectConfig.CORE_TARGET .. "GeneratedContent" }
