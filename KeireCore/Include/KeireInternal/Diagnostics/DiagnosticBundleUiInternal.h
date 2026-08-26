#pragma once

#include "Keire/Ref.h"
#include "Keire/Window.h"
#include "KeireInternal/Diagnostics/DiagnosticBundleInternal.h"

#include <filesystem>
#include <optional>
#include <string>

namespace Keire
{
    class UiFrame;

    namespace Internal
    {
        struct DiagnosticBundleDialogControllerTestAccess;

        class DiagnosticBundleDialogController final
        {
          public:
            DiagnosticBundleDialogController() = default;
            ~DiagnosticBundleDialogController() = default;

            DiagnosticBundleDialogController(const DiagnosticBundleDialogController&) = delete;
            DiagnosticBundleDialogController& operator=(const DiagnosticBundleDialogController&) = delete;
            DiagnosticBundleDialogController(DiagnosticBundleDialogController&&) = delete;
            DiagnosticBundleDialogController& operator=(DiagnosticBundleDialogController&&) = delete;

            void Open(DiagnosticBundleRequest request, std::string defaultName);
            void Draw(UiFrame& ui, WindowSystem& windows, WindowId parent);
            void SetSelection(DiagnosticBundleSelection selection);
            void Shutdown() noexcept;

            [[nodiscard]] const DiagnosticBundleSelection& Selection() const noexcept;
            [[nodiscard]] const FrozenDiagnosticBundle* Bundle() const noexcept;
            [[nodiscard]] const std::string& Status() const noexcept;
            [[nodiscard]] const std::string& Error() const noexcept;

          private:
            friend struct DiagnosticBundleDialogControllerTestAccess;

            void CompleteSaveDialog(SaveFileDialogStatus status, const std::filesystem::path& selectedPath,
                                    std::string diagnostic);
            void Rebuild();
            void PollSaveDialog();

            DiagnosticBundleRequest m_Request;
            std::optional<FrozenDiagnosticBundle> m_Bundle;
            Ref<SaveFileDialogOperation> m_SaveDialog;
            std::string m_DefaultName;
            std::string m_Status;
            std::string m_Error;
            bool m_OpenRequested = false;
            bool m_Visible = false;
        };
    } // namespace Internal
} // namespace Keire
