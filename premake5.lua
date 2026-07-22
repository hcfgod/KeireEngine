local function loadConfig(path)
    local values = {}
    for line in io.lines(path) do
        local key, value = line:match("^([A-Z0-9_]+)=(.*)$")
        if key then
            if values[key] ~= nil then
                error("Duplicate configuration key: " .. key)
            end
            values[key] = value
        elseif line ~= "" then
            error("Malformed configuration line: " .. line)
        end
    end
    return values
end

ProjectConfig = loadConfig("Config/Project.conf")

local requiredKeys = {
    "PROJECT_IDENTIFIER", "PROJECT_DISPLAY_NAME", "PROJECT_VERSION", "PROJECT_NAMESPACE", "PROJECT_MACRO_PREFIX",
    "CORE_TARGET", "CORE_DIRECTORY", "CLIENT_TARGET", "CLIENT_DIRECTORY", "HUB_TARGET", "HUB_DIRECTORY",
    "TESTS_TARGET", "TESTS_DIRECTORY",
    "ARTIFACT_PREFIX", "REPOSITORY_SLUG"
}
for _, key in ipairs(requiredKeys) do
    if ProjectConfig[key] == nil then
        error("Missing configuration key: " .. key)
    end
end

local function validIdentifiers(value, rejectNumericLeadingZero)
    if value == "" or value:sub(1, 1) == "." or value:sub(-1) == "." or value:find("..", 1, true) then
        return false
    end
    for identifier in value:gmatch("[^.]+") do
        if not identifier:match("^[0-9A-Za-z%-]+$") then
            return false
        end
        if rejectNumericLeadingZero and identifier:match("^%d+$") and #identifier > 1 and identifier:sub(1, 1) == "0" then
            return false
        end
    end
    return true
end

local function validSemanticVersion(version)
    if not version then return false end
    local coreAndPre, build = version, nil
    local plus = version:find("+", 1, true)
    if plus then
        coreAndPre = version:sub(1, plus - 1)
        build = version:sub(plus + 1)
        if build:find("+", 1, true) or not validIdentifiers(build, false) then return false end
    end
    local core, prerelease = coreAndPre, nil
    local dash = coreAndPre:find("-", 1, true)
    if dash then
        core = coreAndPre:sub(1, dash - 1)
        prerelease = coreAndPre:sub(dash + 1)
        if not validIdentifiers(prerelease, true) then return false end
    end
    local major, minor, patch = core:match("^(%d+)%.(%d+)%.(%d+)$")
    if not major then return false end
    for _, component in ipairs({ major, minor, patch }) do
        if #component > 1 and component:sub(1, 1) == "0" then return false end
    end
    return true
end

if not validSemanticVersion(ProjectConfig.PROJECT_VERSION) then
    error("PROJECT_VERSION must be a valid Semantic Version 2.0.0 value such as 0.1.0-alpha.1+build.5.")
end

include "Scripts/Premake/Common.lua"
dofile "Build/Generated/Dependencies.lua"

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
AssetToolTarget = ProjectConfig.PROJECT_NAMESPACE .. "AssetTool"
AssetWorkerTarget = ProjectConfig.PROJECT_NAMESPACE .. "AssetWorker"
RuntimeTarget = ProjectConfig.PROJECT_NAMESPACE .. "Runtime"

if _ACTION and _ACTION:match("^vs") and SelectedToolset == "gcc" then
    error("Visual Studio generators do not support the GCC toolset in this template.")
end
if os.host() == "macosx" and _ACTION == "xcode4" and SelectedToolset ~= "clang" then
    error("Xcode generation supports only the Clang toolset.")
end

workspace(ProjectConfig.PROJECT_IDENTIFIER)
    architecture(selectedArchitecture)
    startproject(ProjectConfig.HUB_TARGET)
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

group "Dependencies"
include "Scripts/Premake/DearImGui.lua"
include "Scripts/Premake/Zstd.lua"
include "Scripts/Premake/HeaderDependencies.lua"
group ""

include(ProjectConfig.CORE_DIRECTORY .. "/premake5.lua")
include(ProjectConfig.CLIENT_DIRECTORY .. "/premake5.lua")
include(ProjectConfig.HUB_DIRECTORY .. "/premake5.lua")
include "AssetTool/premake5.lua"
include "KeireAssetWorker/premake5.lua"
include "KeireRuntime/premake5.lua"
include(ProjectConfig.TESTS_DIRECTORY .. "/premake5.lua")
include "KeireEditorTests/premake5.lua"
include "KeireRenderTests/premake5.lua"
