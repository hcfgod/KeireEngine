project(AssetWorkerTarget)
    location "."
    kind "ConsoleApp"

    ApplyCommonProjectSettings()

    files
    {
        "Source/**.cpp"
    }

    includedirs
    {
        "../" .. ProjectConfig.CORE_DIRECTORY .. "/Include"
    }

    local ffmpegRoot = "../Build/Dependencies/ffmpeg"
    local ffmpegDebug = ffmpegRoot .. "/Debug/install"
    local ffmpegRelease = ffmpegRoot .. "/Release/install"
    local commandRepositoryRoot = (_ACTION == "ninja" or _ACTION == "gmake") and "." or ".."
    local workerRuntimeDirectory =
        commandRepositoryRoot .. "/Build/Bin/" .. OutputDir .. "/" .. AssetWorkerTarget
    local function CopyWindowsRuntime(source)
        local windowsSource = source:gsub("/", "\\")
        local windowsRuntimeDirectory = workerRuntimeDirectory:gsub("/", "\\")
        return "if not exist " .. windowsRuntimeDirectory .. " mkdir " .. windowsRuntimeDirectory
            .. " && copy /Y " .. windowsSource .. "\\*.dll " .. windowsRuntimeDirectory .. "\\"
    end
    local function CopyUnixRuntime(source)
        return "mkdir -p " .. workerRuntimeDirectory .. " && cp -R " .. source .. "/. " .. workerRuntimeDirectory .. "/"
    end
    if os.isdir(ffmpegDebug .. "/include") and os.isdir(ffmpegRelease .. "/include") then
        defines { "KEIRE_HAS_FFMPEG=1" }
        externalincludedirs { ffmpegDebug .. "/include" }

        filter { "configurations:Debug or DebugASan or DebugUBSan or DebugTSan or Coverage" }
            libdirs { ffmpegDebug .. "/lib" }
            links { "avformat", "avcodec", "swresample", "avutil" }

        filter { "configurations:Release or Dist" }
            libdirs { ffmpegRelease .. "/lib" }
            links { "avformat", "avcodec", "swresample", "avutil" }

        filter { "system:windows", "configurations:Debug or DebugASan or DebugUBSan or DebugTSan or Coverage" }
            libdirs { ffmpegDebug .. "/bin" }
            prelinkcommands
            {
                CopyWindowsRuntime(commandRepositoryRoot .. "/Build/Dependencies/ffmpeg/Debug/install/bin")
            }

        filter { "system:windows", "configurations:Release or Dist" }
            libdirs { ffmpegRelease .. "/bin" }
            prelinkcommands
            {
                CopyWindowsRuntime(commandRepositoryRoot .. "/Build/Dependencies/ffmpeg/Release/install/bin")
            }

        filter { "system:linux", "configurations:Debug or DebugASan or DebugUBSan or DebugTSan or Coverage" }
            linkoptions { "-Wl,-rpath,$ORIGIN" }
            prelinkcommands
            {
                CopyUnixRuntime(commandRepositoryRoot .. "/Build/Dependencies/ffmpeg/Debug/install/lib")
            }

        filter { "system:linux", "configurations:Release or Dist" }
            linkoptions { "-Wl,-rpath,$ORIGIN" }
            prelinkcommands
            {
                CopyUnixRuntime(commandRepositoryRoot .. "/Build/Dependencies/ffmpeg/Release/install/lib")
            }

        filter { "system:macosx", "configurations:Debug or DebugASan or DebugUBSan or DebugTSan or Coverage" }
            linkoptions { "-Wl,-rpath,@loader_path" }
            prelinkcommands
            {
                CopyUnixRuntime(commandRepositoryRoot .. "/Build/Dependencies/ffmpeg/Debug/install/lib")
            }

        filter { "system:macosx", "configurations:Release or Dist" }
            linkoptions { "-Wl,-rpath,@loader_path" }
            prelinkcommands
            {
                CopyUnixRuntime(commandRepositoryRoot .. "/Build/Dependencies/ffmpeg/Release/install/lib")
            }

        filter {}
    end

    LinkKeireCore()
    LinkSDL3()
