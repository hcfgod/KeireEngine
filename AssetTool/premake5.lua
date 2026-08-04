project(AssetToolTarget)
    location "."
    kind "ConsoleApp"
    dependson { AssetWorkerTarget }

    ApplyCommonProjectSettings()

    files
    {
        "Source/**.cpp"
    }

    includedirs
    {
        "../" .. ProjectConfig.CORE_DIRECTORY .. "/Include"
    }

    externalincludedirs { VendorIncludeDirs.json }

    LinkKeireSourceModules()
    LinkKeireCore()
    LinkSDL3()
