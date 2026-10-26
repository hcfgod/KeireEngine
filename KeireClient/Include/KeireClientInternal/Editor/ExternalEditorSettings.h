#pragma once

#include <filesystem>
#include <string>

namespace Keire
{
    class UiFrame;
}

namespace KeireEditor::Detail
{
    [[nodiscard]] bool DrawCustomExternalEditor(Keire::UiFrame& ui, std::string& draft,
                                                std::filesystem::path& executable, std::string& diagnostic);
}
