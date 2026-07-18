project(DearImGuiProject)
    location "../../Build/Projects/DearImGui"
    kind "StaticLib"
    targetname(DearImGuiLibrary)

    ApplyCommonProjectSettings("../..")

    files
    {
        "DearImGui.lua",
        "../../Vendor/imgui/imconfig.h",
        "../../Vendor/imgui/imgui.h",
        "../../Vendor/imgui/imgui_internal.h",
        "../../Vendor/imgui/imstb_rectpack.h",
        "../../Vendor/imgui/imstb_textedit.h",
        "../../Vendor/imgui/imstb_truetype.h",
        "../../Vendor/imgui/imgui.cpp",
        "../../Vendor/imgui/imgui_demo.cpp",
        "../../Vendor/imgui/imgui_draw.cpp",
        "../../Vendor/imgui/imgui_tables.cpp",
        "../../Vendor/imgui/imgui_widgets.cpp",
        "../../Vendor/imgui/backends/imgui_impl_sdl3.h",
        "../../Vendor/imgui/backends/imgui_impl_sdl3.cpp",
        "../../Vendor/imgui/backends/imgui_impl_sdlgpu3.h",
        "../../Vendor/imgui/backends/imgui_impl_sdlgpu3.cpp",
        "../../Vendor/imgui/misc/cpp/imgui_stdlib.h",
        "../../Vendor/imgui/misc/cpp/imgui_stdlib.cpp"
    }

    vpaths
    {
        ["Core/*"] = {
            "../../Vendor/imgui/imconfig.h",
            "../../Vendor/imgui/imgui.h",
            "../../Vendor/imgui/imgui_internal.h",
            "../../Vendor/imgui/imstb_*.h",
            "../../Vendor/imgui/imgui*.cpp"
        },
        ["Backends/*"] = { "../../Vendor/imgui/backends/imgui_impl_sdl3.*", "../../Vendor/imgui/backends/imgui_impl_sdlgpu3.*" },
        ["Adapters/*"] = { "../../Vendor/imgui/misc/cpp/imgui_stdlib.*" },
        ["Build/*"] = { "DearImGui.lua" }
    }

    externalincludedirs
    {
        "../../Vendor/imgui",
        "../../Vendor/imgui/backends",
        "../../Vendor/imgui/misc/cpp",
        "../" .. DependencyManifest.SDL3Include
    }

    warnings "Off"

    filter {}
