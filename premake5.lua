local function loadConfig(path)
    local values = {}
    for line in io.lines(path) do
        local key, value = line:match("^([A-Z0-9_]+)=(.*)$")
        if key then
            values[key] = value
        end
    end
    return values
end

ProjectConfig = loadConfig("Config/Project.conf")
include "Scripts/Premake/Common.lua"

newoption {
    trigger = "toolset",
    value = "TOOLSET",
    description = "Select the C++ toolset",
    allowed = {
        { "default", "Use the generator default" },
        { "msc", "Microsoft C/C++" },
        { "gcc", "GNU Compiler Collection" },
        { "clang", "Clang" }
    },
    default = "default"
}

newoption {
    trigger = "ci",
    description = "Enable CI-only warnings-as-errors settings"
}

local function resolveToolset(requested)
    if requested ~= "default" then
        return requested
    end
    if os.host() == "windows" then
        if _ACTION == "gmake" then
            return "gcc"
        end
        return "msc"
    end
    if os.host() == "macosx" then
        return "clang"
    end
    return "gcc"
end

local selectedArchitecture = os.targetarch() or os.hostarch()
SelectedToolset = resolveToolset(_OPTIONS["toolset"] or "default")

if _ACTION and _ACTION:match("^vs") and SelectedToolset == "gcc" then
    error("Visual Studio generators do not support the GCC toolset in this template.")
end
if os.host() == "macosx" and _ACTION == "xcode4" and SelectedToolset ~= "clang" then
    error("Xcode generation supports only the Clang toolset.")
end

workspace(ProjectConfig.PROJECT_IDENTIFIER)
    architecture(selectedArchitecture)
    startproject(ProjectConfig.CLIENT_TARGET)
    toolset(SelectedToolset)

    configurations {
        "Debug",
        "Release",
        "Dist",
        "DebugASan",
        "DebugUBSan",
        "DebugTSan",
        "Coverage"
    }

filter "system:windows"
    systemversion "latest"

filter {}

include(ProjectConfig.CORE_DIRECTORY .. "/premake5.lua")
include(ProjectConfig.CLIENT_DIRECTORY .. "/premake5.lua")
include(ProjectConfig.TESTS_DIRECTORY .. "/premake5.lua")
