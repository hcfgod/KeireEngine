#include "KeireClient/Editor/EditorCommandRouter.h"

#include <stdexcept>
#include <utility>

namespace KeireEditor
{
    void EditorCommandRouter::Bind(const EditorCommand command, Action action, Availability available)
    {
        if (!action)
            throw std::invalid_argument("Editor commands require an action.");
        m_Bindings.insert_or_assign(command, Binding{std::move(action), std::move(available)});
    }

    bool EditorCommandRouter::Available(const EditorCommand command) const
    {
        const auto found = m_Bindings.find(command);
        return found != m_Bindings.end() && (!found->second.IsAvailable || found->second.IsAvailable());
    }

    bool EditorCommandRouter::Execute(const EditorCommand command) const
    {
        const auto found = m_Bindings.find(command);
        if (found == m_Bindings.end() || (found->second.IsAvailable && !found->second.IsAvailable()))
            return false;
        found->second.Execute();
        return true;
    }
} // namespace KeireEditor
