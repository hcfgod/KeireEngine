project(ProjectConfig.HUB_TARGET)
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
    AddKeireApplicationIcon()

    includedirs
    {
        "Include",
        "../" .. ProjectConfig.CORE_DIRECTORY .. "/Include"
    }

    defines
    {
        "KEIRE_EDITOR_TARGET=\"" .. ProjectConfig.CLIENT_TARGET .. "\""
    }

    dependson { ProjectConfig.CLIENT_TARGET }

    LinkKeireSourceModules()
    LinkKeireCore()

    LinkSDL3()

    filter "system:windows"
        linkoptions { "/STACK:8388608" }

    filter { "system:windows", "configurations:Dist" }
        kind "WindowedApp"
        entrypoint "mainCRTStartup"

    filter {}
