InstallWorkerTarget = ProjectConfig.PROJECT_NAMESPACE .. "InstallWorker"

project(InstallWorkerTarget)
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
        "../KeireHubRuntime/Include"
    }

    externalincludedirs
    {
        VendorIncludeDirs.json
    }

    links
    {
        HubRuntimeTarget,
        ZstdProject
    }

    defines
    {
        'KEIRE_INSTALL_PRODUCT_IDENTIFIER="' .. ProjectConfig.PROJECT_IDENTIFIER .. '"',
        'KEIRE_INSTALL_PRODUCT_DISPLAY_NAME="' .. ProjectConfig.PROJECT_DISPLAY_NAME .. '"',
        'KEIRE_INSTALL_EDITOR_TARGET="' .. ProjectConfig.CLIENT_TARGET .. '"',
        'KEIRE_INSTALL_HUB_TARGET="' .. ProjectConfig.HUB_TARGET .. '"'
    }

    filter "configurations:Debug or DebugASan"
        defines { "KEIRE_INSTALL_WORKER_FAULT_INJECTION" }

    filter "system:not windows"
        kind "Utility"

    filter "system:windows"
        links { "Advapi32", "Ole32", "Shell32" }

    filter {}

InstallWorkerTestsTarget = ProjectConfig.PROJECT_NAMESPACE .. "InstallWorkerTests"

InstallVerificationFixtureTarget = ProjectConfig.PROJECT_NAMESPACE .. "InstallVerifyFixture"

project(InstallVerificationFixtureTarget)
    location "."
    kind "ConsoleApp"

    ApplyCommonProjectSettings()

    files
    {
        "Fixtures/VerifyInstallationFixture.cpp"
    }

    filter "system:not windows"
        kind "Utility"

    filter {}

project(InstallWorkerTestsTarget)
    location "."
    kind "ConsoleApp"

    ApplyCommonProjectSettings()

    files
    {
        "Tests/**.cpp",
        "../KeireHubTests/Source/InstallTransactionTests.cpp"
    }

    includedirs
    {
        "../KeireHubTests/Include",
        "../KeireHubRuntime/Include"
    }

    externalincludedirs
    {
        VendorIncludeDirs.doctest,
        VendorIncludeDirs.json,
        VendorIncludeDirs.zstd
    }

    links
    {
        HubRuntimeTarget,
        ZstdProject
    }

    filter "configurations:Debug or DebugASan"
        defines { "KEIRE_INSTALL_TRANSACTION_TESTING" }

    filter {}
