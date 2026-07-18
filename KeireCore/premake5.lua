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

    defines { "KEIRE_BUILDING_LIBRARY" }

    filter "system:windows"
        prebuildcommands {
            "if exist Scripts\\Windows\\build-info.ps1 (powershell -NoProfile -ExecutionPolicy Bypass -File Scripts\\Windows\\build-info.ps1) else (powershell -NoProfile -ExecutionPolicy Bypass -File ..\\Scripts\\Windows\\build-info.ps1)"
        }

    filter { "system:linux or macosx" }
        prebuildcommands {
            "if [ -f Scripts/Unix/build-info.sh ]; then bash Scripts/Unix/build-info.sh; else bash ../Scripts/Unix/build-info.sh; fi"
        }

    filter {}

    externalincludedirs
    {
        VendorIncludeDirs.spdlog,
        VendorIncludeDirs.json,
        VendorIncludeDirs.imgui,
        VendorIncludeDirs.imguiBackends,
        VendorIncludeDirs.imguiMisc,
        VendorIncludeDirs.zstd,
        DependencyManifest.SDL3Include
    }

    links { DearImGuiProject, ZstdProject }
