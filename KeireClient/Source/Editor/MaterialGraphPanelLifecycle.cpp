#include "KeireClient/Editor/MaterialGraphPanel.h"

#include <atomic>
#include <stdexcept>
#include <utility>

namespace KeireEditor
{
    MaterialGraphPanel::~MaterialGraphPanel() noexcept
    {
        m_PreviewCancellation->fetch_add(1, std::memory_order_release);
        if (m_PreviewRender)
        {
            m_PreviewRender.Cancel();
            (void)m_PreviewRender.Wait();
        }
        if (m_JobScope)
        {
            m_JobScope->Cancel();
            m_JobScope->Wait();
        }
        if (m_OwnJobSystem && m_JobSystem)
            m_JobSystem->Close();
    }

    void MaterialGraphPanel::SetJobSystem(Keire::Ref<Keire::JobSystem> jobs)
    {
        if (m_PreviewRender || m_JobScope)
            throw std::logic_error("Material Graph preview jobs are already configured.");
        if (!jobs)
            throw std::invalid_argument("Material Graph preview job system is unavailable.");
        m_JobSystem = std::move(jobs);
    }

    void MaterialGraphPanel::Attach(Keire::UiWorkspace& workspace)
    {
        m_Registration = workspace.RegisterPanel({"editor.material-graph", "Material Graph", false});
    }
} // namespace KeireEditor
