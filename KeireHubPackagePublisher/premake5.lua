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
        "../KeireHubRuntime/Include"
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

    LinkKeireHubNativeHttp()
