#include "Keire/Assets/Asset.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <condition_variable>
#include <iomanip>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>

namespace Keire
{
    namespace
    {
        [[nodiscard]] std::string CompactHex(const AssetId id)
        {
            std::ostringstream stream;
            stream << std::hex << std::setfill('0') << std::setw(16) << id.High() << std::setw(16) << id.Low();
            return stream.str();
        }
    } // namespace

    AssetId AssetId::Generate()
    {
        std::random_device source;
        std::uniform_int_distribution<std::uint64_t> distribution;
        std::uint64_t high = distribution(source);
        std::uint64_t low = distribution(source);
        high = (high & 0xffffffffffff0fffULL) | 0x0000000000004000ULL;
        low = (low & 0x3fffffffffffffffULL) | 0x8000000000000000ULL;
        return {high, low};
    }

    AssetId AssetId::Parse(const std::string_view value)
    {
        const bool compactForm = value.size() == 32;
        const bool canonicalForm =
            value.size() == 36 && value[8] == '-' && value[13] == '-' && value[18] == '-' && value[23] == '-';
        if (!compactForm && !canonicalForm)
        {
            throw std::invalid_argument("Asset ID must be 32 hexadecimal digits or canonical UUID form.");
        }
        std::array<char, 32> compact{};
        std::size_t count = 0;
        for (const char character : value)
        {
            if (character == '-')
            {
                continue;
            }
            if (count == compact.size() || !std::isxdigit(static_cast<unsigned char>(character)))
            {
                throw std::invalid_argument("Asset ID must be a canonical 128-bit hexadecimal identifier.");
            }
            compact[count++] = character;
        }
        if (count != compact.size())
        {
            throw std::invalid_argument("Asset ID must contain exactly 32 hexadecimal digits.");
        }

        std::uint64_t high = 0;
        std::uint64_t low = 0;
        const auto highResult = std::from_chars(compact.data(), compact.data() + 16, high, 16);
        const auto lowResult = std::from_chars(compact.data() + 16, compact.data() + compact.size(), low, 16);
        if (highResult.ec != std::errc{} || lowResult.ec != std::errc{})
        {
            throw std::invalid_argument("Asset ID contains invalid hexadecimal digits.");
        }
        return {high, low};
    }

    std::string AssetId::ToString() const
    {
        const std::string compact = CompactHex(*this);
        return compact.substr(0, 8) + '-' + compact.substr(8, 4) + '-' + compact.substr(12, 4) + '-' +
               compact.substr(16, 4) + '-' + compact.substr(20);
    }

    AssetLoadError::AssetLoadError(const AssetId id, AssetDiagnostic diagnostic)
        : std::runtime_error("Asset " + id.ToString() + " failed during " + diagnostic.Operation + ": " +
                             diagnostic.Message),
          m_Id(id), m_Diagnostic(std::move(diagnostic))
    {
    }

    Asset::~Asset() = default;

    BinaryAsset::BinaryAsset(std::vector<std::byte> bytes) : m_Bytes(std::move(bytes)) {}

    TextAsset::TextAsset(std::string text) : m_Text(std::move(text)) {}

    class Detail::AssetHandleState::Impl final
    {
      public:
        Impl(const AssetId id, const AssetTypeId type, Ref<Asset> fallback, const std::uint64_t ownerThreadHash)
            : Id(id), Type(type), Fallback(std::move(fallback)), Current(Fallback), OwnerThreadHash(ownerThreadHash)
        {
        }

        AssetId Id;
        AssetTypeId Type;
        Ref<Asset> Fallback;
        Ref<Asset> Current;
        std::uint64_t OwnerThreadHash;
        AssetState State = AssetState::Queued;
        AssetDiagnostic Diagnostic;
        std::uint64_t Revision = 0;
        bool UsingFallback = true;
        mutable std::mutex Mutex;
        mutable std::condition_variable Changed;
    };

    Detail::AssetHandleState::AssetHandleState(const AssetId id, const AssetTypeId type, Ref<Asset> fallback,
                                               const std::uint64_t ownerThreadHash)
        : m_Impl(std::make_unique<Impl>(id, type, std::move(fallback), ownerThreadHash))
    {
    }

    Detail::AssetHandleState::~AssetHandleState() = default;

    AssetId Detail::AssetHandleState::Id() const noexcept { return m_Impl->Id; }

    AssetTypeId Detail::AssetHandleState::Type() const noexcept { return m_Impl->Type; }

    AssetState Detail::AssetHandleState::State() const noexcept
    {
        std::scoped_lock lock(m_Impl->Mutex);
        return m_Impl->State;
    }

    bool Detail::AssetHandleState::UsingFallback() const noexcept
    {
        std::scoped_lock lock(m_Impl->Mutex);
        return m_Impl->UsingFallback;
    }

    std::uint64_t Detail::AssetHandleState::Revision() const noexcept
    {
        std::scoped_lock lock(m_Impl->Mutex);
        return m_Impl->Revision;
    }

    AssetDiagnostic Detail::AssetHandleState::Diagnostic() const
    {
        std::scoped_lock lock(m_Impl->Mutex);
        return m_Impl->Diagnostic;
    }

    Ref<Asset> Detail::AssetHandleState::Current() const
    {
        std::scoped_lock lock(m_Impl->Mutex);
        return m_Impl->Current;
    }

    void Detail::AssetHandleState::RequireTerminal() const
    {
        std::unique_lock lock(m_Impl->Mutex);
        const auto terminal = [this]
        {
            return m_Impl->State == AssetState::Ready || m_Impl->State == AssetState::Failed ||
                   m_Impl->State == AssetState::Cancelled;
        };
        if (terminal())
            return;
        if (std::hash<std::thread::id>{}(std::this_thread::get_id()) == m_Impl->OwnerThreadHash)
            throw std::logic_error("AssetHandle::Require may not block the asset-system owner thread.");
        m_Impl->Changed.wait(lock,
                             [this]
                             {
                                 return m_Impl->State == AssetState::Ready || m_Impl->State == AssetState::Failed ||
                                        m_Impl->State == AssetState::Cancelled;
                             });
    }

    void Detail::AssetHandleState::SetLoading(const bool reload)
    {
        std::scoped_lock lock(m_Impl->Mutex);
        m_Impl->State = reload ? AssetState::Reloading : AssetState::Loading;
        m_Impl->Diagnostic = {};
    }

    void Detail::AssetHandleState::Commit(Ref<Asset> asset)
    {
        {
            std::scoped_lock lock(m_Impl->Mutex);
            m_Impl->Current = std::move(asset);
            m_Impl->UsingFallback = false;
            m_Impl->State = AssetState::Ready;
            m_Impl->Diagnostic = {};
            ++m_Impl->Revision;
        }
        m_Impl->Changed.notify_all();
    }

    void Detail::AssetHandleState::Fail(AssetDiagnostic diagnostic, const bool reload)
    {
        {
            std::scoped_lock lock(m_Impl->Mutex);
            m_Impl->Diagnostic = std::move(diagnostic);
            if (!reload || m_Impl->UsingFallback)
            {
                m_Impl->Current = m_Impl->Fallback;
                m_Impl->UsingFallback = true;
                m_Impl->State = AssetState::Failed;
            }
            else
            {
                m_Impl->State = AssetState::Ready;
            }
        }
        m_Impl->Changed.notify_all();
    }

    void Detail::AssetHandleState::Cancel()
    {
        {
            std::scoped_lock lock(m_Impl->Mutex);
            if (m_Impl->State == AssetState::Ready || m_Impl->State == AssetState::Failed)
            {
                return;
            }
            m_Impl->State = AssetState::Cancelled;
            m_Impl->Diagnostic = {"cancel", "Asset loading was cancelled during shutdown."};
        }
        m_Impl->Changed.notify_all();
    }
} // namespace Keire
