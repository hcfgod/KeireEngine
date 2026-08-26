project(ProjectConfig.TESTS_TARGET)
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
        "Source/**.cxx",
        "../KeireRuntime/Source/RuntimeUiInput.cpp"
    }

    includedirs
    {
        "Include",
        "../KeireRuntime/Include",
        "../" .. ProjectConfig.CORE_DIRECTORY .. "/Include"
    }

    externalincludedirs
    {
        VendorIncludeDirs.doctest,
        VendorIncludeDirs.spdlog,
        VendorIncludeDirs.imgui,
        VendorIncludeDirs.json
    }

    LinkKeireCore()

    AddKeireManagedRuntimeDependency()

    LinkSDL3()
    ApplyLargeWindowsStack()
