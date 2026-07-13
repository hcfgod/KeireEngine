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
        "Include",
        "../Build/Generated"
    }

    defines { "CORE_BUILDING_LIBRARY" }

    externalincludedirs
    {
        VendorIncludeDirs.spdlog
    }
