#pragma once

#include "Keire/Api.h"
#include "Keire/Application.h"
#include "Keire/Assert.h"
#include "Keire/Assets/Asset.h"
#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/AssetSystem.h"
#include "Keire/Assets/InputActionAsset.h"
#include "Keire/BuildInfo.h"
#include "Keire/EntryPoint.h"
#include "Keire/Event.h"
#include "Keire/Input/Input.h"
#include "Keire/Layer.h"
#include "Keire/Log.h"
#include "Keire/Project/Project.h"
#include "Keire/Ref.h"
#include "Keire/Scenes/Scene.h"
#include "Keire/Scenes/SceneAsset.h"
#include "Keire/Scenes/SceneSystem.h"
#include "Keire/Time.h"
#include "Keire/Ui.h"
#include "Keire/UiWorkspace.h"
#include "Keire/Window.h"
#include "Keire/WindowConfig.h"

namespace Keire
{
    [[nodiscard]] KEIRE_API const char* GetName() noexcept;
}
