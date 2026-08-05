#include "Keire/Modules/EngineModule.h"

#include "Keire/Application.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <map>
#include <ranges>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace Keire
{
    namespace
    {
        [[nodiscard]] bool ValidModuleId(const std::string_view value)
        {
            if (value.empty())
                return false;
            return std::ranges::all_of(value,
                                       [](const char character)
                                       {
                                           return (character >= 'a' && character <= 'z') ||
                                                  (character >= '0' && character <= '9') || character == '.' ||
                                                  character == '-' || character == '_';
                                       });
        }

        [[nodiscard]] std::uint32_t ParseComponent(const std::string_view value)
        {
            if (value.empty())
                throw std::invalid_argument("Module version contains an empty component.");
            std::uint32_t result = 0;
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
                throw std::invalid_argument("Module version must use major.minor.patch integers.");
            return result;
        }

        void IncrementVersionComponent(std::uint32_t& component)
        {
            if (component == (std::numeric_limits<std::uint32_t>::max)())
                throw std::invalid_argument("Module version range upper bound overflows.");
            ++component;
        }

        template <typename T, typename Key>
        void RequireUnique(const std::vector<T>& values, Key key, const std::string_view category)
        {
            std::set<std::string> seen;
            for (const auto& value : values)
                if (!seen.emplace(key(value)).second)
                    throw std::invalid_argument("Duplicate " + std::string(category) + " registration.");
        }
    } // namespace

    ModuleVersion ModuleVersion::Parse(const std::string_view value)
    {
        std::array<std::uint32_t, 3> parts{};
        std::size_t begin = 0;
        for (std::size_t index = 0; index < parts.size(); ++index)
        {
            const auto end = value.find('.', begin);
            if (index < 2 && end == std::string_view::npos)
                throw std::invalid_argument("Module version must contain major, minor, and patch components.");
            if (index == 2 && end != std::string_view::npos)
                throw std::invalid_argument("Module version contains too many components.");
            parts[index] =
                ParseComponent(value.substr(begin, end == std::string_view::npos ? value.size() - begin : end - begin));
            begin = end == std::string_view::npos ? value.size() : end + 1;
        }
        return {parts[0], parts[1], parts[2]};
    }

    std::string ModuleVersion::ToString() const
    {
        return std::to_string(Major) + '.' + std::to_string(Minor) + '.' + std::to_string(Patch);
    }

    ModuleVersionRange ModuleVersionRange::Parse(const std::string_view value)
    {
        if (value == "*")
            return {};
        ModuleVersionRange result;
        if (!value.empty() && value.front() == '^')
        {
            result.MinimumInclusive = ModuleVersion::Parse(value.substr(1));
            auto maximum = *result.MinimumInclusive;
            if (maximum.Major > 0)
            {
                IncrementVersionComponent(maximum.Major);
                maximum.Minor = 0;
                maximum.Patch = 0;
            }
            else if (maximum.Minor > 0)
            {
                IncrementVersionComponent(maximum.Minor);
                maximum.Patch = 0;
            }
            else
                IncrementVersionComponent(maximum.Patch);
            result.MaximumExclusive = maximum;
            return result;
        }
        if (!value.starts_with(">="))
        {
            const auto exact = ModuleVersion::Parse(value);
            result.MinimumInclusive = exact;
            auto maximum = exact;
            IncrementVersionComponent(maximum.Patch);
            result.MaximumExclusive = maximum;
            return result;
        }
        const auto separator = value.find(' ');
        result.MinimumInclusive = ModuleVersion::Parse(
            value.substr(2, separator == std::string_view::npos ? value.size() - 2 : separator - 2));
        if (separator != std::string_view::npos)
        {
            const auto maximum = value.substr(separator + 1);
            if (!maximum.starts_with('<'))
                throw std::invalid_argument("Module version ranges support only an exclusive upper bound.");
            result.MaximumExclusive = ModuleVersion::Parse(maximum.substr(1));
            if (*result.MaximumExclusive <= *result.MinimumInclusive)
                throw std::invalid_argument("Module version range upper bound must exceed its lower bound.");
        }
        return result;
    }

    bool ModuleVersionRange::Contains(const ModuleVersion version) const noexcept
    {
        return (!MinimumInclusive || version >= *MinimumInclusive) &&
               (!MaximumExclusive || version < *MaximumExclusive);
    }

    std::string ModuleVersionRange::ToString() const
    {
        if (!MinimumInclusive && !MaximumExclusive)
            return "*";
        std::string result = MinimumInclusive ? ">=" + MinimumInclusive->ToString() : "";
        if (MaximumExclusive)
            result += (result.empty() ? "" : " ") + std::string("<") + MaximumExclusive->ToString();
        return result;
    }

    class ModuleRegistrationContext::Impl final
    {
      public:
        std::vector<ComponentRegistration> Components;
        std::vector<AssetImporterRegistration> Importers;
        std::vector<AssetDecoderRegistration> Decoders;
        std::vector<ReplaySerializerRegistration> ReplaySerializers;
        std::vector<DiagnosticDefinition> Diagnostics;
        std::vector<ProjectUpgradeStep> ProjectUpgrades;
        std::vector<ModuleMemoryDomainRegistration> MemoryDomains;
        bool Open = true;
    };

    ModuleRegistrationContext::ModuleRegistrationContext(std::unique_ptr<Impl> implementation)
        : m_Impl(std::move(implementation))
    {
    }

    ModuleRegistrationContext::~ModuleRegistrationContext() = default;

    void ModuleRegistrationContext::RegisterComponent(ComponentRegistration registration)
    {
        if (!m_Impl->Open)
            throw std::logic_error("Module registration is frozen.");
        m_Impl->Components.push_back(std::move(registration));
    }

    void ModuleRegistrationContext::RegisterImporter(AssetImporterRegistration registration)
    {
        if (!m_Impl->Open)
            throw std::logic_error("Module registration is frozen.");
        m_Impl->Importers.push_back(std::move(registration));
    }

    void ModuleRegistrationContext::RegisterDecoder(AssetDecoderRegistration registration)
    {
        if (!m_Impl->Open)
            throw std::logic_error("Module registration is frozen.");
        m_Impl->Decoders.push_back(std::move(registration));
    }

    void ModuleRegistrationContext::RegisterReplaySerializer(ReplaySerializerRegistration registration)
    {
        if (!m_Impl->Open)
            throw std::logic_error("Module registration is frozen.");
        m_Impl->ReplaySerializers.push_back(std::move(registration));
    }

    void ModuleRegistrationContext::RegisterDiagnostic(DiagnosticDefinition definition)
    {
        if (!m_Impl->Open)
            throw std::logic_error("Module registration is frozen.");
        m_Impl->Diagnostics.push_back(std::move(definition));
    }

    void ModuleRegistrationContext::RegisterProjectUpgrade(ProjectUpgradeStep step)
    {
        if (!m_Impl->Open)
            throw std::logic_error("Module registration is frozen.");
        m_Impl->ProjectUpgrades.push_back(std::move(step));
    }

    void ModuleRegistrationContext::RegisterMemoryDomain(ModuleMemoryDomainRegistration domain)
    {
        if (!m_Impl->Open)
            throw std::logic_error("Module registration is frozen.");
        m_Impl->MemoryDomains.push_back(std::move(domain));
    }

    EngineModule::~EngineModule() = default;

    class ModuleRegistry::Impl final
    {
      public:
        explicit Impl(ModuleRegistrySpecification&& specification)
        {
            std::map<std::string, std::pair<ModuleDescriptor, Ref<EngineModule>>, std::less<>> candidates;
            for (auto& module : specification.Modules)
            {
                if (!module)
                    throw std::invalid_argument("Module registry received a null module.");
                auto descriptor = module->Descriptor();
                if (!ValidModuleId(descriptor.Id) || descriptor.DisplayName.empty())
                    throw std::invalid_argument("Module descriptor has an invalid ID or display name.");
                if (!candidates.emplace(descriptor.Id, std::pair{descriptor, module}).second)
                    throw std::invalid_argument("Duplicate source module ID: " + descriptor.Id);
            }

            enum class Visit : std::uint8_t
            {
                Visiting,
                Complete
            };
            std::map<std::string, Visit, std::less<>> visits;
            std::function<void(const std::string&)> visit = [&](const std::string& id)
            {
                if (const auto state = visits.find(id); state != visits.end())
                {
                    if (state->second == Visit::Visiting)
                        throw std::invalid_argument("Source module dependency cycle includes: " + id);
                    return;
                }
                visits.emplace(id, Visit::Visiting);
                auto& [descriptor, module] = candidates.at(id);
                std::ranges::sort(descriptor.Dependencies, {}, &ModuleDependency::Id);
                for (const auto& dependency : descriptor.Dependencies)
                {
                    const auto found = candidates.find(dependency.Id);
                    if (found == candidates.end())
                        throw std::invalid_argument("Source module " + id + " requires missing module " +
                                                    dependency.Id + '.');
                    if (!dependency.Version.Contains(found->second.first.Version))
                        throw std::invalid_argument("Source module " + id + " has an incompatible dependency " +
                                                    dependency.Id + '.');
                    visit(dependency.Id);
                }
                visits[id] = Visit::Complete;
                OrderedDescriptors.push_back(descriptor);
                OrderedModules.push_back(module);
            };
            for (const auto& [id, ignored] : candidates)
            {
                (void)ignored;
                visit(id);
            }

            auto context = ModuleRegistrationContext(std::make_unique<ModuleRegistrationContext::Impl>());
            for (const auto& module : OrderedModules)
                module->Register(context);
            context.m_Impl->Open = false;
            Registrations = std::move(context.m_Impl);
            ValidateRegistrations();
        }

        void ValidateRegistrations() const
        {
            RequireUnique(
                Registrations->Components, [](const auto& value) { return value.Type.ToString(); }, "component");
            RequireUnique(Registrations->Importers, [](const auto& value) { return value.Name; }, "importer");
            RequireUnique(Registrations->Decoders, [](const auto& value) { return value.Type.ToString(); }, "decoder");
            RequireUnique(
                Registrations->ReplaySerializers, [](const auto& value) { return value.Id; }, "replay serializer");
            RequireUnique(
                Registrations->Diagnostics, [](const auto& value) { return std::string(value.Id.Value()); },
                "diagnostic");
            RequireUnique(
                Registrations->ProjectUpgrades, [](const auto& value) { return value.Id; }, "project upgrade");
            RequireUnique(Registrations->MemoryDomains, [](const auto& value) { return value.Name; }, "memory domain");
        }

        std::vector<ModuleDescriptor> OrderedDescriptors;
        std::vector<Ref<EngineModule>> OrderedModules;
        std::unique_ptr<ModuleRegistrationContext::Impl> Registrations;
        std::size_t StartedCount = 0;
        bool Started = false;
        bool EverStarted = false;
    };

    ModuleRegistry::ModuleRegistry(ModuleRegistrySpecification specification)
        : m_Impl(std::make_unique<Impl>(std::move(specification)))
    {
    }

    ModuleRegistry::~ModuleRegistry() { Close(); }

    std::vector<ModuleDescriptor> ModuleRegistry::OrderedCatalog() const { return m_Impl->OrderedDescriptors; }

    std::string ModuleRegistry::Fingerprint() const
    {
        std::ostringstream result;
        for (const auto& module : m_Impl->OrderedDescriptors)
            result << module.Id << '@' << module.Version.ToString() << '\n';
        return result.str();
    }

    void ModuleRegistry::ValidateRequired(const std::span<const RequiredSourceModule> required) const
    {
        for (const auto& requirement : required)
        {
            const auto found = std::ranges::find(m_Impl->OrderedDescriptors, requirement.Id, &ModuleDescriptor::Id);
            if (found == m_Impl->OrderedDescriptors.end())
                throw std::runtime_error("Project requires source module " + requirement.Id + '.');
            const auto range = ModuleVersionRange::Parse(requirement.VersionRange);
            if (!range.Contains(found->Version))
                throw std::runtime_error("Project source module version mismatch for " + requirement.Id + '.');
        }
    }

    void ModuleRegistry::ValidateCatalog(const std::span<const RequiredSourceModule> catalog) const
    {
        if (catalog.size() != m_Impl->OrderedDescriptors.size())
            throw std::runtime_error("Source module catalog does not match this host.");
        for (std::size_t index = 0; index < catalog.size(); ++index)
            if (catalog[index].Id != m_Impl->OrderedDescriptors[index].Id ||
                ModuleVersion::Parse(catalog[index].VersionRange) != m_Impl->OrderedDescriptors[index].Version)
                throw std::runtime_error("Source module catalog does not match this host.");
    }

    std::vector<ComponentRegistration> ModuleRegistry::Components() const { return m_Impl->Registrations->Components; }

    std::vector<AssetImporterRegistration> ModuleRegistry::Importers() const
    {
        return m_Impl->Registrations->Importers;
    }

    std::vector<AssetDecoderRegistration> ModuleRegistry::Decoders() const { return m_Impl->Registrations->Decoders; }

    std::vector<ReplaySerializerRegistration> ModuleRegistry::ReplaySerializers() const
    {
        return m_Impl->Registrations->ReplaySerializers;
    }

    std::vector<DiagnosticDefinition> ModuleRegistry::Diagnostics() const { return m_Impl->Registrations->Diagnostics; }

    std::vector<ProjectUpgradeStep> ModuleRegistry::ProjectUpgrades() const
    {
        return m_Impl->Registrations->ProjectUpgrades;
    }

    std::vector<ModuleMemoryDomainRegistration> ModuleRegistry::MemoryDomains() const
    {
        return m_Impl->Registrations->MemoryDomains;
    }

    void ModuleRegistry::Start(Application& application)
    {
        if (m_Impl->EverStarted)
            throw std::logic_error("Source modules may be started exactly once.");
        m_Impl->Started = true;
        m_Impl->EverStarted = true;
        try
        {
            for (const auto& module : m_Impl->OrderedModules)
            {
                module->OnStart(application);
                ++m_Impl->StartedCount;
            }
        }
        catch (...)
        {
            Close();
            throw;
        }
    }

    bool ModuleRegistry::IsStarted() const noexcept { return m_Impl->Started; }

    void ModuleRegistry::Close() noexcept
    {
        while (m_Impl && m_Impl->StartedCount != 0)
        {
            --m_Impl->StartedCount;
            m_Impl->OrderedModules[m_Impl->StartedCount]->OnStop();
        }
        if (m_Impl)
            m_Impl->Started = false;
    }
} // namespace Keire
