#include "KeireHub/HubWindowVisibility.h"

#include "Keire/Ref.h"

#include <doctest/doctest.h>

#include <stdexcept>
#include <string>

namespace
{
    enum class VisibilityFailure
    {
        None,
        Show,
        Restore,
        Raise
    };

    class VisibilityWindow final : public Keire::Window
    {
      public:
        [[nodiscard]] Keire::WindowId Id() const noexcept override { return Keire::WindowId(1); }
        [[nodiscard]] Keire::WindowSpecification Specification() const override { return {}; }
        [[nodiscard]] Keire::LogicalExtent LogicalSize() const override { return {}; }
        [[nodiscard]] Keire::PixelExtent PixelSize() const override { return {}; }
        [[nodiscard]] Keire::WindowPosition Position() const override { return {}; }
        [[nodiscard]] float DisplayScale() const override { return 1.0F; }
        [[nodiscard]] std::string Title() const override { return "Hub visibility regression"; }
        [[nodiscard]] bool Focused() const override { return m_Focused; }
        [[nodiscard]] bool Visible() const override { return m_Visible; }
        [[nodiscard]] bool Minimized() const override { return m_Minimized; }
        [[nodiscard]] bool Maximized() const override { return false; }
        [[nodiscard]] Keire::WindowMode Mode() const override { return Keire::WindowMode::Windowed; }
        [[nodiscard]] bool CloseRequested() const override { return false; }
        [[nodiscard]] bool IsOpen() const override { return true; }
        void SetTitle(std::string) override {}
        void SetSize(Keire::LogicalExtent) override {}
        void SetVisible(const bool visible) override
        {
            if (visible && m_Failure == VisibilityFailure::Show)
                throw std::runtime_error("show failed");
            m_Visible = visible;
            if (!visible)
                m_Focused = false;
        }
        void Minimize() override
        {
            m_Minimized = true;
            m_Focused = false;
        }
        void Maximize() override {}
        void Restore() override
        {
            if (m_Failure == VisibilityFailure::Restore)
                throw std::runtime_error("restore failed");
            // A hidden native window retains its minimized placement until restored while visible.
            if (m_Visible)
                m_Minimized = false;
        }
        void Raise() override
        {
            if (m_Failure == VisibilityFailure::Raise)
                throw std::runtime_error("raise failed");
            m_Focused = m_Visible && !m_Minimized;
        }
        void SetMode(Keire::WindowMode) override {}
        void Close() override {}
        void FailAt(const VisibilityFailure failure) { m_Failure = failure; }

      private:
        bool m_Visible = false;
        bool m_Minimized = true;
        bool m_Focused = false;
        VisibilityFailure m_Failure = VisibilityFailure::None;
    };
} // namespace

TEST_CASE("Hub window restoration reveals and restores hidden or minimized windows")
{
    auto window = Keire::CreateRef<VisibilityWindow>();
    SUBCASE("hidden after an editor launch") {}
    SUBCASE("already visible and minimized") { window->SetVisible(true); }
    SUBCASE("already visible and restored")
    {
        window->SetVisible(true);
        window->Restore();
    }

    KeireHub::RestoreHubWindow(*window);
    CHECK(window->Visible());
    CHECK_FALSE(window->Minimized());
    CHECK(window->Focused());

    KeireHub::RestoreHubWindow(*window);
    CHECK(window->Visible());
    CHECK_FALSE(window->Minimized());
    CHECK(window->Focused());
}

TEST_CASE("Hub window restoration preserves native failures and permits retry")
{
    auto window = Keire::CreateRef<VisibilityWindow>();
    SUBCASE("show fails")
    {
        window->FailAt(VisibilityFailure::Show);
        CHECK_THROWS_WITH_AS(KeireHub::RestoreHubWindow(*window), "show failed", std::runtime_error);
        CHECK_FALSE(window->Visible());
        CHECK(window->Minimized());
    }
    SUBCASE("restore fails")
    {
        window->FailAt(VisibilityFailure::Restore);
        CHECK_THROWS_WITH_AS(KeireHub::RestoreHubWindow(*window), "restore failed", std::runtime_error);
        CHECK(window->Visible());
        CHECK(window->Minimized());
    }
    SUBCASE("raise fails")
    {
        window->FailAt(VisibilityFailure::Raise);
        CHECK_THROWS_WITH_AS(KeireHub::RestoreHubWindow(*window), "raise failed", std::runtime_error);
        CHECK(window->Visible());
        CHECK_FALSE(window->Minimized());
    }
    CHECK_FALSE(window->Focused());
    window->FailAt(VisibilityFailure::None);
    KeireHub::RestoreHubWindow(*window);
    CHECK(window->Visible());
    CHECK_FALSE(window->Minimized());
    CHECK(window->Focused());
}
