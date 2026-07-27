project(ProjectConfig.CLIENT_TARGET)
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
        "../" .. ProjectConfig.CORE_DIRECTORY .. "/Include"
    }

    LinkKeireCore()

    dependson { AssetWorkerTarget, KeireManagedProject }

    filter "system:windows"
        prebuildcommands
        {
            'if not exist "%{cfg.objdir}" mkdir "%{cfg.objdir}"',
            'powershell -NoProfile -ExecutionPolicy Bypass -File ' ..
                (_ACTION == "ninja" and "Scripts/Windows/build-managed.ps1"
                    or "../Scripts/Windows/build-managed.ps1")
        }

    filter { "system:linux or macosx" }
        prebuildcommands
        {
            '"../Build/Dependencies/dotnet-sdk/dotnet" build "../KeireManaged/Keire.Managed.csproj" ' ..
                '--nologo --configuration Release --output "../Build/Managed" ' ..
                '--property:BaseIntermediateOutputPath="../Build/Intermediates/Managed/"'
        }

    filter {}

    postbuildcommands
    {
        '{MKDIR} "%{cfg.targetdir}/Managed"',
        '{COPYFILE} "../Build/Managed/Keire.Managed.dll" "%{cfg.targetdir}/Managed/Keire.Managed.dll"'
    }

    LinkSDL3()
