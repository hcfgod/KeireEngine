include "Scripts/Premake/Common.lua"

workspace "CrossPlatformCoreClientTemplate"
    architecture "x86_64"
    startproject "Client"

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
