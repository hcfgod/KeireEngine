#pragma once

#include "Keire/Assets/Asset.h"
#include "Keire/Ref.h"
#include "Keire/Undo.h"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace KeireEditor
{
    enum class AssetDocumentReloadResult : std::uint8_t
    {
        Applied,
        Unchanged,
        LocalChanges
    };

    template <typename Definition>
        requires std::default_initializable<Definition> && std::copy_constructible<Definition> &&
                 std::equality_comparable<Definition>
    class AssetDocumentHost final
    {
      public:
        struct Specification final
        {
            std::function<void(const Definition&)> Validate;
            std::function<std::vector<std::byte>(const Definition&)> Encode;
            std::function<void(Keire::AssetId, const Definition&)> Preview;
            std::function<void(Keire::AssetId)> CancelPreview;
            std::function<void(Keire::AssetId, std::span<const std::byte>)> Persist;
        };

        explicit AssetDocumentHost(Specification specification) : m_Specification(std::move(specification))
        {
            if (!m_Specification.Validate || !m_Specification.Encode || !m_Specification.Persist)
                throw std::invalid_argument("Asset document validation, encoding, and persistence are required.");
        }

        ~AssetDocumentHost() { Close(); }

        AssetDocumentHost(const AssetDocumentHost&) = delete;
        AssetDocumentHost& operator=(const AssetDocumentHost&) = delete;

        void Open(const Keire::AssetId asset, Definition definition, const std::uint64_t revision,
                  Keire::Ref<Keire::UndoContext> undo = {})
        {
            if (!asset || revision == 0)
                throw std::invalid_argument("Opening an asset document requires an asset and revision.");
            m_Specification.Validate(definition);
            Close();
            m_Asset = asset;
            m_Baseline = definition;
            m_Draft = std::move(definition);
            m_Revision = revision;
            m_Undo = std::move(undo);
            if (m_Undo && m_Undo->IsOpen())
                m_Undo->Clear();
            m_New = false;
            ++m_Serial;
            try
            {
                BeginLifetime();
                ApplyPreview(m_Draft);
            }
            catch (...)
            {
                Close();
                throw;
            }
        }

        void Create(const Keire::AssetId asset, Definition definition, Keire::Ref<Keire::UndoContext> undo = {})
        {
            if (!asset)
                throw std::invalid_argument("Creating an asset document requires an asset identity.");
            m_Specification.Validate(definition);
            Close();
            m_Asset = asset;
            m_Baseline = definition;
            m_Draft = std::move(definition);
            m_Revision = 0;
            m_Undo = std::move(undo);
            if (m_Undo && m_Undo->IsOpen())
                m_Undo->Clear();
            m_New = true;
            ++m_Serial;
            try
            {
                BeginLifetime();
                ApplyPreview(m_Draft);
            }
            catch (...)
            {
                Close();
                throw;
            }
        }

        [[nodiscard]] bool IsOpen() const noexcept { return static_cast<bool>(m_Asset); }
        [[nodiscard]] Keire::AssetId Asset() const noexcept { return m_Asset; }
        [[nodiscard]] std::uint64_t Revision() const noexcept { return m_Revision; }
        [[nodiscard]] const Definition& Draft() const
        {
            RequireOpen();
            return m_Draft;
        }
        [[nodiscard]] const Definition& Baseline() const
        {
            RequireOpen();
            return m_Baseline;
        }
        [[nodiscard]] bool Dirty() const noexcept { return IsOpen() && (m_New || !(m_Draft == m_Baseline)); }
        [[nodiscard]] std::string_view Diagnostic() const noexcept { return m_Diagnostic; }
        [[nodiscard]] Keire::Ref<Keire::UndoContext> UndoContext() const noexcept { return m_Undo; }

        bool Edit(const std::string_view name, Definition candidate)
        {
            RequireOpen();
            if (name.empty())
                throw std::invalid_argument("An asset document edit requires an undo name.");
            if (candidate == m_Draft)
                return false;
            const auto before = m_Draft;
            Apply(candidate);
            RecordApplied(name, before, std::move(candidate));
            return true;
        }

        void Save()
        {
            RequireOpen();
            m_Specification.Validate(m_Draft);
            const auto bytes = m_Specification.Encode(m_Draft);
            try
            {
                m_Specification.Persist(m_Asset, bytes);
            }
            catch (const std::exception& error)
            {
                m_Diagnostic = error.what();
                throw;
            }
            m_Baseline = m_Draft;
            m_New = false;
            m_Diagnostic.clear();
        }

        void Discard()
        {
            RequireOpen();
            if (m_New)
            {
                Close();
                return;
            }
            Apply(m_Baseline);
            if (m_Undo && m_Undo->IsOpen())
                m_Undo->Clear();
            m_Diagnostic.clear();
        }

        [[nodiscard]] AssetDocumentReloadResult Reload(Definition definition, const std::uint64_t revision)
        {
            RequireOpen();
            if (revision == 0)
                throw std::invalid_argument("Asset document reload requires a revision.");
            if (revision == m_Revision)
                return AssetDocumentReloadResult::Unchanged;
            if (Dirty())
                return AssetDocumentReloadResult::LocalChanges;
            Apply(definition);
            m_Baseline = m_Draft;
            m_Revision = revision;
            if (m_Undo && m_Undo->IsOpen())
                m_Undo->Clear();
            m_Diagnostic.clear();
            return AssetDocumentReloadResult::Applied;
        }

        void AcknowledgeRevision(const std::uint64_t revision)
        {
            RequireOpen();
            if (revision == 0)
                throw std::invalid_argument("An asset document revision must be non-zero.");
            m_Revision = revision;
        }

        [[nodiscard]] bool Undo() { return m_Undo && m_Undo->Undo(); }
        [[nodiscard]] bool Redo() { return m_Undo && m_Undo->Redo(); }

        void Close() noexcept
        {
            EndLifetime();
            if (m_Asset && m_Specification.CancelPreview)
            {
                try
                {
                    m_Specification.CancelPreview(m_Asset);
                }
                catch (...)
                {
                }
            }
            m_Asset = {};
            m_Revision = 0;
            m_Undo.Reset();
            m_Diagnostic.clear();
            m_New = false;
            ++m_Serial;
        }

      private:
        struct LifetimeState final
        {
            AssetDocumentHost* Owner = nullptr;
            std::uint64_t Serial = 0;
        };

        void BeginLifetime() { m_Lifetime = std::make_shared<LifetimeState>(LifetimeState{this, m_Serial}); }

        void EndLifetime() noexcept
        {
            if (m_Lifetime)
                m_Lifetime->Owner = nullptr;
            m_Lifetime.reset();
        }

        void RequireOpen() const
        {
            if (!IsOpen())
                throw std::logic_error("The asset document is not open.");
        }

        void ApplyPreview(const Definition& definition)
        {
            if (m_Specification.Preview)
                m_Specification.Preview(m_Asset, definition);
        }

        void Apply(const Definition& definition)
        {
            m_Specification.Validate(definition);
            try
            {
                ApplyPreview(definition);
            }
            catch (...)
            {
                const auto failure = std::current_exception();
                try
                {
                    ApplyPreview(m_Draft);
                }
                catch (...)
                {
                }
                try
                {
                    std::rethrow_exception(failure);
                }
                catch (const std::exception& error)
                {
                    m_Diagnostic = error.what();
                    throw;
                }
            }
            m_Draft = definition;
            m_Diagnostic.clear();
        }

        void RecordApplied(const std::string_view name, Definition before, Definition after)
        {
            if (!m_Undo || !m_Undo->IsOpen())
                return;
            const std::weak_ptr<LifetimeState> lifetime = m_Lifetime;
            const auto estimated = m_Specification.Encode(m_Draft).size();
            m_Undo->RecordApplied(Keire::CreateUndoCommand(
                std::string(name),
                [lifetime, after]
                {
                    if (const auto state = lifetime.lock(); state && state->Owner)
                        state->Owner->ApplyIfCurrent(state->Serial, after);
                },
                [lifetime, before]
                {
                    if (const auto state = lifetime.lock(); state && state->Owner)
                        state->Owner->ApplyIfCurrent(state->Serial, before);
                },
                estimated,
                [lifetime]
                {
                    const auto state = lifetime.lock();
                    return state && state->Owner && state->Owner->IsOpen() && state->Owner->m_Serial == state->Serial;
                }));
        }

        void ApplyIfCurrent(const std::uint64_t serial, const Definition& definition)
        {
            if (!IsOpen() || m_Serial != serial)
                return;
            Apply(definition);
        }

        Specification m_Specification;
        Keire::AssetId m_Asset;
        Definition m_Baseline{};
        Definition m_Draft{};
        std::uint64_t m_Revision = 0;
        std::uint64_t m_Serial = 1;
        Keire::Ref<Keire::UndoContext> m_Undo;
        std::shared_ptr<LifetimeState> m_Lifetime;
        std::string m_Diagnostic;
        bool m_New = false;
    };
} // namespace KeireEditor
