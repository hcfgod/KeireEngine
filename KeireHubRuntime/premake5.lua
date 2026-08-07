HubRuntimeTarget = ProjectConfig.PROJECT_NAMESPACE .. "HubRuntime"

project(HubRuntimeTarget)
    location "."
    kind "StaticLib"

    ApplyCommonProjectSettings()

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

    filter { "system:macosx", "files:**NativeHttpTransportMac.mm" }
        buildoptions { "-fobjc-arc" }

    filter {}
