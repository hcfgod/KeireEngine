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

local selectedArchitecture = os.targetarch() or os.hostarch()
local selectedToolset = _OPTIONS["toolset"] or "default"

if _ACTION and _ACTION:match("^vs") and selectedToolset == "gcc" then
    error("Visual Studio generators do not support the GCC toolset in this template.")
end
if os.host() == "macosx" and _ACTION == "xcode4" and selectedToolset ~= "default" and selectedToolset ~= "clang" then
    error("Xcode generation supports only the default or Clang toolset.")
end

workspace "CrossPlatformCoreClientTemplate"
    architecture(selectedArchitecture)
    startproject "Client"

    if selectedToolset ~= "default" then
        toolset(selectedToolset)
    end

    configurations
    {
        "Debug",
        "Release",
        "Dist",
        "DebugASan",
        "DebugUBSan",
        "DebugTSan"
    }

filter "system:windows"
    systemversion "latest"

filter {}

include "Core/premake5.lua"
include "Client/premake5.lua"
include "Tests/premake5.lua"
