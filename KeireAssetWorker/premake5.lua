project(AssetWorkerTarget)
    location "."
    kind "ConsoleApp"

    ApplyCommonProjectSettings()

    files
    {
        "Include/**.h",
        "Source/**.cpp"
    }

    includedirs
    {
        "Include",
        "../" .. ProjectConfig.CORE_DIRECTORY .. "/Include"
    }

    local ffmpegRoot = "../Build/Dependencies/ffmpeg"
    local ffmpegDebug = ffmpegRoot .. "/Debug/install"
    local ffmpegRelease = ffmpegRoot .. "/Release/install"
    local commandRepositoryRoot = (_ACTION == "ninja" or _ACTION == "gmake") and "." or ".."
    local workerRuntimeDirectory =
        commandRepositoryRoot .. "/Build/Bin/" .. OutputDir .. "/" .. AssetWorkerTarget
    local workerPrelinkStamp = commandRepositoryRoot .. "/Build/Intermediates/" .. OutputDir .. "/" ..
                                   AssetWorkerTarget .. "/" .. AssetWorkerTarget .. ".prelinkevents"
    local function CopyWindowsRuntime(source)
        local command = "powershell -NoProfile -ExecutionPolicy Bypass -File " .. commandRepositoryRoot ..
                            "/Scripts/Windows/copy-files-if-changed.ps1 -SourceDirectory " .. source ..
                            " -DestinationDirectory " .. workerRuntimeDirectory .. " -Filter *.dll"
        if _ACTION == "ninja" then
            command = command .. " && powershell -NoProfile -ExecutionPolicy Bypass -File " .. commandRepositoryRoot ..
                          "/Scripts/Windows/touch-ninja-stamp.ps1 -Path " .. workerPrelinkStamp .. " && exit /b 0"
        end
        return command
    end
    local function CopyUnixRuntime(source)
        return "bash " .. commandRepositoryRoot .. "/Scripts/Unix/copy-files-if-changed.sh " .. source .. " " ..
                   workerRuntimeDirectory .. " '*'"
    end
    if os.isdir(ffmpegDebug .. "/include") then
        filter { "configurations:Debug or DebugASan or DebugUBSan or DebugTSan or Coverage" }
            defines { "KEIRE_HAS_FFMPEG=1" }
            externalincludedirs { ffmpegDebug .. "/include" }
            libdirs { GeneratorRootPath(ffmpegDebug .. "/lib") }
            links { "avformat", "avcodec", "swresample", "avutil" }

        filter { "system:windows", "configurations:Debug or DebugASan or DebugUBSan or DebugTSan or Coverage" }
            libdirs { GeneratorRootPath(ffmpegDebug .. "/bin") }
            prelinkcommands
            {
                CopyWindowsRuntime(commandRepositoryRoot .. "/Build/Dependencies/ffmpeg/Debug/install/bin")
            }

        filter { "system:linux", "configurations:Debug or DebugASan or DebugUBSan or DebugTSan or Coverage" }
            linkoptions { "-Wl,-rpath,'$$ORIGIN'" }
            prelinkcommands
            {
                CopyUnixRuntime(commandRepositoryRoot .. "/Build/Dependencies/ffmpeg/Debug/install/lib")
            }

        filter { "system:macosx", "configurations:Debug or DebugASan or DebugUBSan or DebugTSan or Coverage" }
            linkoptions { "-Wl,-rpath,@loader_path" }
            prelinkcommands
            {
                CopyUnixRuntime(commandRepositoryRoot .. "/Build/Dependencies/ffmpeg/Debug/install/lib")
            }

        filter {}
    end

    if os.isdir(ffmpegRelease .. "/include") then
        filter { "configurations:Release or Dist" }
            defines { "KEIRE_HAS_FFMPEG=1" }
            externalincludedirs { ffmpegRelease .. "/include" }
            libdirs { GeneratorRootPath(ffmpegRelease .. "/lib") }
            links { "avformat", "avcodec", "swresample", "avutil" }

        filter { "system:windows", "configurations:Release or Dist" }
            libdirs { GeneratorRootPath(ffmpegRelease .. "/bin") }
            prelinkcommands
            {
                CopyWindowsRuntime(commandRepositoryRoot .. "/Build/Dependencies/ffmpeg/Release/install/bin")
            }

        filter { "system:linux", "configurations:Release or Dist" }
            linkoptions { "-Wl,-rpath,'$$ORIGIN'" }
            prelinkcommands
            {
                CopyUnixRuntime(commandRepositoryRoot .. "/Build/Dependencies/ffmpeg/Release/install/lib")
            }

        filter { "system:macosx", "configurations:Release or Dist" }
            linkoptions { "-Wl,-rpath,@loader_path" }
            prelinkcommands
            {
                CopyUnixRuntime(commandRepositoryRoot .. "/Build/Dependencies/ffmpeg/Release/install/lib")
            }

        filter {}
    end

    LinkKeireSourceModules()
    LinkKeireCore()
    LinkSDL3()
