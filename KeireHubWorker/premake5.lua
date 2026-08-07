project(HubWorkerTarget)
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

    links
    {
        HubRuntimeTarget,
        ZstdProject
    }

    LinkKeireHubNativeHttp()
