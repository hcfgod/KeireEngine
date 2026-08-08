HubTestsTarget = ProjectConfig.PROJECT_NAMESPACE .. "HubTests"

project(HubTestsTarget)
    location "."
    kind "ConsoleApp"

    ApplyCommonProjectSettings()

    files
    {
        "Include/**.h",
        "Source/**.cpp",
        "../KeireHub/Source/HubBuildSupportInventoryWorkflow.cpp",
        "../KeireHub/Source/HubChromeLayout.cpp",
        "../KeireHub/Source/HubAccountWorkflow.cpp",
        "../KeireHub/Source/HubEditorDiscovery.cpp",
        "../KeireHub/Source/HubInstance.cpp",
        "../KeireHub/Source/HubEditorInstallWorkflow.cpp",
        "../KeireHub/Source/HubEditorManagementWorkflow.cpp",
        "../KeireHub/Source/HubFirstRunImportPreparation.cpp",
        "../KeireHub/Source/HubFirstRunIntegration.cpp",
        "../KeireHub/Source/HubFirstRunWorkflow.cpp",
        "../KeireHub/Source/HubMaintenanceWorkflow.cpp",
        "../KeireHub/Source/HubPathMigration.cpp",
        "../KeireHub/Source/HubProjectMetadataWorkflow.cpp",
        "../KeireHub/Source/HubProjectRegistrationWorkflow.cpp",
        "../KeireHub/Source/HubProjectUiState.cpp",
        "../KeireHub/Source/HubProjectMutationWorkflow.cpp",
        "../KeireHub/Source/HubProjectUpgradeWorkflow.cpp",
        "../KeireHub/Source/HubTemplateBrowser.cpp",
        "../KeireHub/Source/HubUpdateHandoffWorkflow.cpp"
    }

    includedirs
    {
        "Include",
        "../KeireHub/Include",
        "../KeireHubRuntime/Include",
        "../" .. ProjectConfig.CORE_DIRECTORY .. "/Include"
    }

    externalincludedirs
    {
        VendorIncludeDirs.doctest,
        VendorIncludeDirs.json,
        VendorIncludeDirs.zstd
    }

    links
    {
        HubRuntimeTarget
    }

    LinkKeireCore()
    LinkSDL3()

    dependson
    {
        HubWorkerTarget
    }

    defines
    {
        "KEIRE_EDITOR_TARGET=\"" .. ProjectConfig.CLIENT_TARGET .. "\"",
        'KEIRE_HUB_WORKER_TEST_EXECUTABLE="Build/Bin/' .. OutputDir .. '/' .. HubWorkerTarget .. '/' .. HubWorkerTarget .. '"'
    }

    LinkKeireHubNativeHttp()

    filter "system:windows"
        links { "user32" }

    filter "system:linux"
        links { "dl" }

    filter {}
