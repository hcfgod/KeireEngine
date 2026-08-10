HubPackagePublisherTarget = ProjectConfig.PROJECT_NAMESPACE .. "HubPackagePublisher"

project(HubPackagePublisherTarget)
    location "."
    kind "ConsoleApp"

    ApplyCommonProjectSettings()

    files
    {
        "Source/**.cpp"
    }

    includedirs
    {
        "../KeireHubRuntime/Include",
        "../" .. ProjectConfig.CORE_DIRECTORY .. "/Include"
    }

    externalincludedirs
    {
        VendorIncludeDirs.json,
        VendorIncludeDirs.zstd
    }

    links
    {
        HubRuntimeTarget,
        ZstdProject
    }

    LinkKeireCore()
    LinkSDL3()

    LinkKeireHubNativeHttp()
    ApplyLargeWindowsStack()
