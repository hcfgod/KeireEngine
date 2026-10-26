#pragma once

namespace KeireEditor::Detail
{
    enum class SettingsEditSurface
    {
        None,
        Project,
        Environment
    };

    // Observe each settings surface once per frame, including hidden/closed surfaces.
    // A boundary must be committed before drawing editable controls on the incoming surface.
    class SettingsEditSurfaceTracker final
    {
      public:
        [[nodiscard]] bool Observe(const SettingsEditSurface surface, const bool visible, const bool focused)
        {
            if (surface == SettingsEditSurface::None)
            {
                return false;
            }
            if (visible && focused)
            {
                const bool boundary = m_Active != SettingsEditSurface::None && m_Active != surface;
                m_Active = surface;
                return boundary;
            }
            if (m_Active == surface)
            {
                m_Active = SettingsEditSurface::None;
                return true;
            }
            return false;
        }

      private:
        SettingsEditSurface m_Active = SettingsEditSurface::None;
    };
} // namespace KeireEditor::Detail
