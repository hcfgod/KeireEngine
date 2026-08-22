project(ProjectConfig.PROJECT_NAMESPACE .. "EditorTests")
    location "."
    kind "ConsoleApp"

    ApplyCommonProjectSettings()

    files
    {
        "Include/**.h",
        "Source/**.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/AssetOperationService.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/AssetPackageAuthoring.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/AssetPicker.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/AssetBrowserFolderCache.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/AssetBrowserUtilities.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/AuthoringWidgetState.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/AuthoringWidgets.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/AuthoringValueEditors.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/AudioMixerDocument.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/ConsolePanel.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/EditorCommandRouter.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/EditorSessionState.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/EditorWindowPlacement.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/EulerEditContinuity.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/ExternalEditorProfiles.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/GraphAuthoringUtilities.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/GraphCanvasValidation.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/GraphComments.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/GraphClipboard.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/GraphDuplication.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/GraphLayout.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/GraphNavigation.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/GraphNodeAnnotations.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/GraphSelection.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/InputActionsDocument.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/InspectorPropertyVisibility.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/MaterialDocument.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/MaterialGraphDocument.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/MaterialGraphCreationPicker.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/NamedAssetCreation.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/PackageImportReview.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/ShaderGraphDocument.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/ShaderGraphPublication.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/ShaderGraphPreview.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/ShaderGraphPreviewGeometry.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/ShaderGraphPreviewRaster.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/ShaderGraphPreviewTextureSampling.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/MaterialInspectorPanel.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/ManagedDataDocument.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/PrefabAuthoring.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/PropertyDrawerRegistry.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/ProjectSettingsDocument.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/SceneCameraController.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/SceneDocument.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/SceneGizmoController.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/ScenePicker.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/ScenePlayChanges.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/ScenePlayChangesPanel.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/SceneTransitionCoordinator.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/ThumbnailService.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/VfxEffectDocument.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/VfxEmitterInspector.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/VfxNodeCatalog.cpp",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Source/Editor/ViewportAssetDropRouter.cpp"
    }

    includedirs
    {
        "Include",
        "../" .. ProjectConfig.CLIENT_DIRECTORY .. "/Include",
        "../" .. ProjectConfig.CORE_DIRECTORY .. "/Include"
    }

    externalincludedirs
    {
        VendorIncludeDirs.doctest,
        VendorIncludeDirs.json
    }

    LinkKeireCore()
    dependson { AssetWorkerTarget }
    LinkSDL3()
