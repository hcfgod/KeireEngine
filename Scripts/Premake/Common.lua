OutputDir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

VendorIncludeDirs = {
    spdlog = "../Vendor/spdlog/include",
    doctest = "../Vendor/doctest"
}

function ApplyCommonProjectSettings()
    language "C++"
    cppdialect "C++20"
    staticruntime "off"

    targetdir ("../Build/Bin/" .. OutputDir .. "/%{prj.name}")
    objdir ("../Build/Intermediates/" .. OutputDir .. "/%{prj.name}")

    filter "system:windows"
        buildoptions { "/utf-8" }

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
            "SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO"
        }

    filter "configurations:Dist"
        runtime "Release"
        optimize "full"
        symbols "off"
        defines
        {
            "SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO"
        }

    ApplySanitizerSettings()

    filter {}
end

function ApplySanitizerSettings()
    filter { "configurations:DebugASan", "system:windows" }
        editandcontinue "Off"
        buildoptions { "/fsanitize=address" }

    filter { "configurations:DebugASan", "system:windows", "kind:ConsoleApp" }
        linkoptions { "/INCREMENTAL:NO" }

    filter { "configurations:DebugASan", "system:linux or macosx" }
        buildoptions { "-fsanitize=address", "-fno-omit-frame-pointer" }
        linkoptions { "-fsanitize=address" }

    filter { "configurations:DebugUBSan", "system:linux or macosx" }
        buildoptions { "-fsanitize=undefined", "-fno-omit-frame-pointer" }
        linkoptions { "-fsanitize=undefined" }

    filter { "configurations:DebugTSan", "system:linux or macosx" }
        buildoptions { "-fsanitize=thread", "-fno-omit-frame-pointer" }
        linkoptions { "-fsanitize=thread" }

    filter {}
end
