EditorDevTarget = ProjectConfig.PROJECT_NAMESPACE .. "EditorDev"

project(EditorDevTarget)
    location "../../Build/Projects/KeireEditorDev"
    kind "StaticLib"

    ApplyCommonProjectSettings("../..")

    files { "Source/EditorDevAnchor.cpp" }
    dependson { ProjectConfig.CLIENT_TARGET, AssetToolTarget, RuntimeTarget }

    targetname(ProjectConfig.PROJECT_NAMESPACE .. "EditorDevProxy")
