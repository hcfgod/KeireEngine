OutputDir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

VendorIncludeDirs = {
    spdlog = "../Vendor/spdlog/include",
    doctest = "../Vendor/doctest"
}

function ApplyCommonProjectSettings()
    language "C++"
    cppdialect "C++20"
    staticruntime "off"
    warnings "Extra"

    targetdir ("../Build/Bin/" .. OutputDir .. "/%{prj.name}")
    objdir ("../Build/Intermediates/" .. OutputDir .. "/%{prj.name}")
    debugdir "../"

    filter "options:ci"
        fatalwarnings "All"

    filter {}

    local selectedToolset = _OPTIONS["toolset"] or "default"
    local usesMsvcCommandLine = os.host() == "windows" and
        ((_ACTION and _ACTION:match("^vs")) or (_ACTION == "ninja" and (selectedToolset == "default" or selectedToolset == "msc")))
    if usesMsvcCommandLine then
        buildoptions { "/utf-8" }
    end

    filter { "configurations:Debug or DebugASan or DebugUBSan or DebugTSan" }
        runtime "Debug"
        symbols "on"
        defines
        {
            "CORE_LOG_DEFAULT_TRACE",
            "SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE"
        }

    filter "configurations:Release"
        runtime "Release"
        optimize "on"
        defines
        {
            "NDEBUG",
            "SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO"
        }

    filter "configurations:Dist"
        runtime "Release"
        optimize "full"
        symbols "off"
        defines
        {
            "NDEBUG",
            "SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO"
        }

    ApplySanitizerSettings()

    filter {}
end

function ApplySanitizerSettings()
    filter { "configurations:DebugASan", "toolset:msc" }
        editandcontinue "Off"
        buildoptions { "/fsanitize=address" }

    filter { "configurations:DebugASan", "toolset:msc", "kind:ConsoleApp" }
        linkoptions { "/INCREMENTAL:NO" }

    filter { "configurations:DebugASan", "toolset:gcc or clang" }
        buildoptions { "-fsanitize=address", "-fno-omit-frame-pointer" }
        linkoptions { "-fsanitize=address" }

    filter { "configurations:DebugUBSan", "toolset:gcc or clang" }
        buildoptions { "-fsanitize=undefined", "-fno-omit-frame-pointer" }
        linkoptions { "-fsanitize=undefined" }

    filter { "configurations:DebugTSan", "toolset:gcc or clang" }
        buildoptions { "-fsanitize=thread", "-fno-omit-frame-pointer" }
        linkoptions { "-fsanitize=thread" }

    filter {}
end
