project(ProjectConfig.CORE_TARGET)
    location "."
    kind "StaticLib"

    ApplyCommonProjectSettings()

    files
    {
        "Include/**.h",
        "Include/**.hpp",
        "Source/**.c",
        "Source/**.cc",
        "Source/**.cpp",
        "Source/**.cxx"
    }

    includedirs
    {
        "Include"
    }

    externalincludedirs
    {
        VendorIncludeDirs.spdlog
    }
