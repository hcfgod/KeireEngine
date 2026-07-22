project(ProjectConfig.PROJECT_NAMESPACE .. "EditorTests")
    location "."
    kind "ConsoleApp"

    ApplyCommonProjectSettings()

    files
    {
        "Source/**.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/EditorCommandRouter.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/EditorWindowPlacement.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/InputActionsDocument.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/MaterialDocument.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/MaterialInspectorPanel.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/PropertyDrawerRegistry.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/SceneCameraController.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/SceneDocument.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/SceneGizmoController.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/ScenePicker.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/ScenePlayChanges.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/ScenePlayChangesPanel.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/ThumbnailService.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/ViewportAssetDropRouter.cpp"
    }

    includedirs
    {
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Include",
        "../" .. ProjectConfig.CORE_DIRECTORY .. "/Include"
    }

    externalincludedirs
    {
        VendorIncludeDirs.doctest
    }

    LinkKeireCore()
    LinkSDL3()
