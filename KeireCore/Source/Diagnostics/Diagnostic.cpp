#include "Keire/Diagnostics/Diagnostic.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <deque>
#include <map>
#include <mutex>
#include <stdexcept>

namespace Keire
{
    namespace
    {
        [[nodiscard]] bool ValidDiagnosticId(const std::string_view value) noexcept
        {
            if (!value.starts_with("KEIRE-") || value.size() < 15)
                return false;
            const auto finalDash = value.rfind('-');
            if (finalDash == std::string_view::npos || finalDash <= 6 || value.size() - finalDash - 1 != 4)
                return false;
            for (std::size_t index = 6; index < finalDash; ++index)
            {
                const auto character = static_cast<unsigned char>(value[index]);
                if (!(std::isupper(character) || std::isdigit(character) || character == '-'))
                    return false;
            }
            return std::ranges::all_of(value.substr(finalDash + 1), [](const char character)
                                       { return std::isdigit(static_cast<unsigned char>(character)) != 0; });
        }
    } // namespace

    DiagnosticId::DiagnosticId(const std::string_view value) : m_Value(value)
    {
        if (!ValidDiagnosticId(m_Value))
            throw std::invalid_argument("Diagnostic IDs must use KEIRE-<DOMAIN>-NNNN.");
    }

    std::optional<DiagnosticId> DiagnosticId::TryParse(const std::string_view value) noexcept
    {
        if (!ValidDiagnosticId(value))
            return std::nullopt;
        try
        {
            return DiagnosticId(value);
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    bool DiagnosticId::IsValid() const noexcept { return ValidDiagnosticId(m_Value); }

    std::string_view DiagnosticId::Value() const noexcept { return m_Value; }

    class DiagnosticCatalog::Impl final
    {
      public:
        explicit Impl(DiagnosticSystemSpecification value) : Specification(std::move(value)) {}

        DiagnosticSystemSpecification Specification;
        mutable std::mutex Mutex;
        std::map<std::string, DiagnosticDefinition, std::less<>> Entries;
        bool Frozen = false;
    };

    DiagnosticCatalog::DiagnosticCatalog(DiagnosticSystemSpecification specification)
        : m_Impl(std::make_unique<Impl>(std::move(specification)))
    {
    }

    DiagnosticCatalog::~DiagnosticCatalog() = default;

    void DiagnosticCatalog::Register(DiagnosticDefinition definition)
    {
        if (!definition.Id.IsValid())
            throw std::invalid_argument("A diagnostic definition requires a valid ID.");
        if (definition.Title.empty())
            throw std::invalid_argument("A diagnostic definition requires a title.");
        std::scoped_lock lock(m_Impl->Mutex);
        if (m_Impl->Frozen)
            throw std::logic_error("The diagnostic catalog is frozen.");
        const auto [entry, inserted] =
            m_Impl->Entries.emplace(std::string(definition.Id.Value()), std::move(definition));
        if (!inserted)
            throw std::invalid_argument("The diagnostic ID is already registered: " + entry->first);
    }

    void DiagnosticCatalog::Register(const std::span<const DiagnosticDefinition> definitions)
    {
        std::scoped_lock lock(m_Impl->Mutex);
        if (m_Impl->Frozen)
            throw std::logic_error("The diagnostic catalog is frozen.");
        auto candidate = m_Impl->Entries;
        for (const auto& definition : definitions)
        {
            if (!definition.Id.IsValid() || definition.Title.empty())
                throw std::invalid_argument("Every diagnostic definition requires a valid ID and title.");
            if (!candidate.emplace(std::string(definition.Id.Value()), definition).second)
                throw std::invalid_argument("A diagnostic ID is registered more than once.");
        }
        m_Impl->Entries = std::move(candidate);
    }

    void DiagnosticCatalog::Freeze()
    {
        std::scoped_lock lock(m_Impl->Mutex);
        m_Impl->Frozen = true;
    }

    bool DiagnosticCatalog::IsFrozen() const noexcept
    {
        std::scoped_lock lock(m_Impl->Mutex);
        return m_Impl->Frozen;
    }

    std::optional<DiagnosticDefinition> DiagnosticCatalog::Find(const DiagnosticId& id) const
    {
        std::scoped_lock lock(m_Impl->Mutex);
        const auto found = m_Impl->Entries.find(id.Value());
        return found == m_Impl->Entries.end() ? std::nullopt : std::optional<DiagnosticDefinition>(found->second);
    }

    std::vector<DiagnosticDefinition> DiagnosticCatalog::Definitions() const
    {
        std::scoped_lock lock(m_Impl->Mutex);
        std::vector<DiagnosticDefinition> result;
        result.reserve(m_Impl->Entries.size());
        for (const auto& [id, definition] : m_Impl->Entries)
        {
            (void)id;
            result.push_back(definition);
        }
        return result;
    }

    std::optional<std::filesystem::path> DiagnosticCatalog::LocalDocumentation(const DiagnosticId& id) const
    {
        const auto definition = Find(id);
        if (!definition)
            return std::nullopt;
        const auto relative = definition->DocumentationPath.empty()
                                  ? std::filesystem::path(std::string(id.Value()) + ".md")
                                  : definition->DocumentationPath;
        return m_Impl->Specification.PackagedDocumentationRoot / relative;
    }

    std::optional<std::string> DiagnosticCatalog::OnlineDocumentation(const DiagnosticId& id) const
    {
        const auto definition = Find(id);
        if (!definition)
            return std::nullopt;
        const auto relative = definition->DocumentationPath.empty() ? std::string(id.Value()) + ".md"
                                                                    : definition->DocumentationPath.generic_string();
        auto root = m_Impl->Specification.OnlineDocumentationRoot;
        if (!root.empty() && root.back() != '/')
            root.push_back('/');
        return root + relative;
    }

    class DiagnosticSink::Impl final
    {
      public:
        explicit Impl(const std::size_t maximum) : Maximum(maximum) {}

        mutable std::mutex Mutex;
        std::deque<Diagnostic> Diagnostics;
        std::size_t Maximum = 0;
        std::atomic<std::uint64_t> NextSequence{1};
        std::atomic<std::size_t> Dropped{0};
    };

    DiagnosticSink::DiagnosticSink(const std::size_t maximumRetainedDiagnostics)
        : m_Impl(std::make_unique<Impl>(maximumRetainedDiagnostics))
    {
        if (maximumRetainedDiagnostics == 0)
            throw std::invalid_argument("The diagnostic retention capacity must be greater than zero.");
    }

    DiagnosticSink::~DiagnosticSink() = default;

    std::uint64_t DiagnosticSink::Report(Diagnostic diagnostic) noexcept
    {
        try
        {
            diagnostic.Sequence = m_Impl->NextSequence.fetch_add(1, std::memory_order_relaxed);
            const auto sequence = diagnostic.Sequence;
            std::scoped_lock lock(m_Impl->Mutex);
            if (m_Impl->Diagnostics.size() == m_Impl->Maximum)
            {
                m_Impl->Diagnostics.pop_front();
                m_Impl->Dropped.fetch_add(1, std::memory_order_relaxed);
            }
            m_Impl->Diagnostics.push_back(std::move(diagnostic));
            return sequence;
        }
        catch (...)
        {
            m_Impl->Dropped.fetch_add(1, std::memory_order_relaxed);
            return 0;
        }
    }

    std::vector<Diagnostic> DiagnosticSink::Snapshot() const
    {
        std::scoped_lock lock(m_Impl->Mutex);
        return {m_Impl->Diagnostics.begin(), m_Impl->Diagnostics.end()};
    }

    std::vector<Diagnostic> DiagnosticSink::Since(const std::uint64_t sequence) const
    {
        std::scoped_lock lock(m_Impl->Mutex);
        std::vector<Diagnostic> result;
        for (const auto& diagnostic : m_Impl->Diagnostics)
        {
            if (diagnostic.Sequence > sequence)
                result.push_back(diagnostic);
        }
        return result;
    }

    std::size_t DiagnosticSink::DroppedCount() const noexcept
    {
        return m_Impl->Dropped.load(std::memory_order_relaxed);
    }

    void DiagnosticSink::Clear() noexcept
    {
        std::scoped_lock lock(m_Impl->Mutex);
        m_Impl->Diagnostics.clear();
    }
} // namespace Keire
