#include "KeireClientInternal/Editor/InputActionsContextReadiness.h"

#include "Keire/Assets/InputActionAsset.h"
#include "Keire/Assets/RenderingAssets.h"

#include <doctest/doctest.h>

TEST_CASE("Input Actions context waits for catalog publication without hiding mounted type errors")
{
    using KeireEditor::Detail::EvaluateInputActionsContextReadiness;
    using KeireEditor::Detail::InputActionsContextReadiness;

    CHECK(EvaluateInputActionsContextReadiness(std::nullopt, Keire::InputActionAsset::StaticType()) ==
          InputActionsContextReadiness::WaitingForCatalog);
    CHECK(EvaluateInputActionsContextReadiness(Keire::InputActionAsset::StaticType(),
                                               Keire::InputActionAsset::StaticType()) ==
          InputActionsContextReadiness::Ready);
    CHECK(EvaluateInputActionsContextReadiness(Keire::Texture2DAsset::StaticType(),
                                               Keire::InputActionAsset::StaticType()) ==
          InputActionsContextReadiness::WrongType);
}
