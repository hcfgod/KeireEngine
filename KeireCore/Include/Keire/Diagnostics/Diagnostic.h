#pragma once

#include "Keire/Api.h"
#include "Keire/Ref.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Keire
{
    class KEIRE_API DiagnosticId final
    {
      public:
        DiagnosticId() noexcept = default;
        explicit DiagnosticId(std::string_view value);

        [[nodiscard]] static std::optional<DiagnosticId> TryParse(std::string_view value) noexcept;
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] std::string_view Value() const noexcept;
        [[nodiscard]] bool operator==(const DiagnosticId&) const noexcept = default;

      private:
        std::string m_Value;
    };

    enum class DiagnosticSeverity : std::uint8_t
    {
        Information,
        Warning,
        Error,
        Fatal
    };

    struct DiagnosticLocation
    {
        std::filesystem::path File;
        std::size_t Line = 0;
        std::size_t Column = 0;
    };

    struct DiagnosticDefinition
    {
        DiagnosticId Id;
        std::string Title;
        std::string Summary;
        std::filesystem::path DocumentationPath;
    };

    struct Diagnostic
    {
        DiagnosticId Id;
        DiagnosticSeverity Severity = DiagnosticSeverity::Error;
        std::string Message;
        std::optional<DiagnosticLocation> Location;
        std::vector<std::pair<std::string, std::string>> Context;
        std::uint64_t Sequence = 0;
    };

    struct DiagnosticSystemSpecification
    {
        std::size_t MaximumRetainedDiagnostics = 4096;
        std::filesystem::path PackagedDocumentationRoot = "docs/Diagnostics";
        std::string OnlineDocumentationRoot = "https://github.com/hcfgod/KeireEngine/blob/main/docs/Diagnostics";
    };

    class KEIRE_API DiagnosticCatalog final : public RefCounted
    {
      public:
        explicit DiagnosticCatalog(DiagnosticSystemSpecification specification = {});
        ~DiagnosticCatalog() override;

        DiagnosticCatalog(const DiagnosticCatalog&) = delete;
        DiagnosticCatalog& operator=(const DiagnosticCatalog&) = delete;

        void Register(DiagnosticDefinition definition);
        void Register(std::span<const DiagnosticDefinition> definitions);
        void Freeze();
        [[nodiscard]] bool IsFrozen() const noexcept;
        [[nodiscard]] std::optional<DiagnosticDefinition> Find(const DiagnosticId& id) const;
        [[nodiscard]] std::vector<DiagnosticDefinition> Definitions() const;
        [[nodiscard]] std::optional<std::filesystem::path> LocalDocumentation(const DiagnosticId& id) const;
        [[nodiscard]] std::optional<std::string> OnlineDocumentation(const DiagnosticId& id) const;

      private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };

    class KEIRE_API DiagnosticSink final : public RefCounted
    {
      public:
        explicit DiagnosticSink(std::size_t maximumRetainedDiagnostics = 4096);
        ~DiagnosticSink() override;

        DiagnosticSink(const DiagnosticSink&) = delete;
        DiagnosticSink& operator=(const DiagnosticSink&) = delete;

        std::uint64_t Report(Diagnostic diagnostic) noexcept;
        [[nodiscard]] std::vector<Diagnostic> Snapshot() const;
        [[nodiscard]] std::vector<Diagnostic> Since(std::uint64_t sequence) const;
        [[nodiscard]] std::size_t DroppedCount() const noexcept;
        void Clear() noexcept;

      private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Keire
