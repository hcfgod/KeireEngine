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

    filter "system:windows"
        linkoptions { "/STACK:8388608" }

    filter {}
