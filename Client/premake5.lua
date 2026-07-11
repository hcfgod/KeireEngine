project "Client"
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
        "Source/**.cxx"
    }

    includedirs
    {
        "Include",
        "../Core/Include"
    }

    externalincludedirs
    {
        VendorIncludeDirs.spdlog
    }

    links
    {
        "Core"
    }
