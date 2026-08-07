project(ProjectConfig.HUB_TARGET)
    location "."
    kind "ConsoleApp"

    ApplyCommonProjectSettings()

    files
    {
        "Include/**.h",
        "Include/**.hpp",
        "Source/**.c",
        "Source/**.cc",
        "Source/**.cpp",
        "Source/**.cxx",
        "Source/**.h"
    }
    AddKeireApplicationIcon()

    includedirs
    {
        "Include",
        "../KeireHubRuntime/Include",
        "../" .. ProjectConfig.CORE_DIRECTORY .. "/Include"
    }

    externalincludedirs
    {
        VendorIncludeDirs.json
    }

    defines
    {
        "KEIRE_EDITOR_TARGET=\"" .. ProjectConfig.CLIENT_TARGET .. "\"",
        "KEIRE_HUB_WORKER_TARGET=\"" .. HubWorkerTarget .. "\""
    }

    dependson { HubWorkerTarget }

    links { HubRuntimeTarget }

    LinkKeireHubNativeHttp()

    LinkKeireSourceModules()
    LinkKeireCore()

    LinkSDL3()

    filter "system:windows"
        links { "Bcrypt", "Wintrust" }
        linkoptions { "/STACK:8388608" }

    filter { "system:windows", "configurations:Dist" }
        kind "WindowedApp"
        entrypoint "mainCRTStartup"

    filter {}
