HubRuntimeTarget = ProjectConfig.PROJECT_NAMESPACE .. "HubRuntime"

project(HubRuntimeTarget)
    location "."
    kind "StaticLib"

    ApplyCommonProjectSettings()

    pchheader "KeireHubRuntime/HubRuntimePch.h"
    pchsource "Source/HubRuntimePch.cpp"

    filter { "system:windows", "toolset:msc" }
        buildoptions { "/FIKeireHubRuntime/HubRuntimePch.h" }

    filter "configurations:Debug or DebugASan"
        defines { "KEIRE_INSTALL_TRANSACTION_TESTING" }

    filter { "system:linux or macosx" }
        buildoptions { "-include KeireHubRuntime/HubRuntimePch.h" }

    filter {}

    files
    {
        "Include/**.h",
        "Source/**.cpp"
    }

    includedirs
    {
        "Include"
    }

    externalincludedirs
    {
        VendorIncludeDirs.json,
        VendorIncludeDirs.stb,
        VendorIncludeDirs.zstd
    }

    links { ZstdProject }

    filter "system:macosx"
        files { "Source/**.mm" }

    filter { "files:**.mm" }
        enablepch "Off"

    filter { "system:macosx", "files:**NativeHttpTransportMac.mm" }
        buildoptions { "-fobjc-arc" }

    filter {}
