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

    local clientCommandPrefix = _ACTION == "ninja" and "KeireClient/" or ""

    filter "system:windows"
        prebuildcommands
        {
            'if not exist "' .. clientCommandPrefix .. '%{cfg.objdir}" mkdir "' ..
                clientCommandPrefix .. '%{cfg.objdir}"',
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
        '{MKDIR} "' .. clientCommandPrefix .. '%{cfg.targetdir}/Managed"',
        '{COPYFILE} "' .. clientCommandPrefix .. '../Build/Managed/Keire.Managed.dll" "' ..
            clientCommandPrefix .. '%{cfg.targetdir}/Managed/Keire.Managed.dll"'
    }

    LinkSDL3()
