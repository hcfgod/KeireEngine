project(AssetToolTarget)
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

    LinkKeireCore()
    LinkSDL3()
