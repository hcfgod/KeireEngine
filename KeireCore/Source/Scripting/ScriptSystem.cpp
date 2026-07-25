#include "Keire/Scripting/ScriptSystem.h"

#include "Keire/ECS/Component.h"
#include "Keire/ECS/Entity.h"
#include "KeireInternal/FileSystem.h"
#include "KeireInternal/Process.h"

#include <Coral/Assembly.hpp>
#include <Coral/Attribute.hpp>
#include <Coral/HostInstance.hpp>
#include <Coral/ManagedObject.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <ranges>
#include <regex>
#include <set>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>

namespace Keire
{
    namespace
    {
        [[nodiscard]] std::string PathText(const std::filesystem::path& path)
        {
            const auto value = path.generic_u8string();
            return {reinterpret_cast<const char*>(value.data()), value.size()};
        }

        [[nodiscard]] std::string XmlEscape(const std::string_view value)
        {
            std::string result;
            result.reserve(value.size());
            for (const char character : value)
            {
                switch (character)
                {
                case '&':
                    result += "&amp;";
                    break;
                case '<':
                    result += "&lt;";
                    break;
                case '>':
                    result += "&gt;";
                    break;
                case '\"':
                    result += "&quot;";
                    break;
                default:
                    result.push_back(character);
                    break;
                }
            }
            return result;
        }

        void WriteText(const std::filesystem::path& path, const std::string_view value)
        {
            std::filesystem::create_directories(path.parent_path());
            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            if (!stream || !stream.write(value.data(), static_cast<std::streamsize>(value.size())))
                throw std::runtime_error("Managed build could not write '" + PathText(path) + "'.");
        }

        [[nodiscard]] bool HasDotnet10Sdk(const std::filesystem::path& executable)
        {
            const auto sdk = executable.parent_path() / "sdk";
            if (!std::filesystem::is_directory(sdk))
                return false;
            return std::ranges::any_of(std::filesystem::directory_iterator(sdk),
                                       [](const std::filesystem::directory_entry& entry)
                                       {
                                           const auto name = entry.path().filename().string();
                                           return entry.is_directory() && name.starts_with("10.");
                                       });
        }

        [[nodiscard]] std::filesystem::path ResolveDotnet(const std::filesystem::path& configured)
        {
            if (!configured.empty())
            {
                const auto resolved = std::filesystem::absolute(configured).lexically_normal();
                if (!std::filesystem::is_regular_file(resolved) || !HasDotnet10Sdk(resolved))
                    throw std::runtime_error("The configured dotnet executable does not provide the .NET 10 SDK.");
                return resolved;
            }

#if defined(_WIN32)
            char* rawDotnetRoot = nullptr;
            std::size_t dotnetRootSize = 0;
            if (_dupenv_s(&rawDotnetRoot, &dotnetRootSize, "DOTNET_ROOT") != 0)
                rawDotnetRoot = nullptr;
            const std::unique_ptr<char, decltype(&std::free)> dotnetRoot(rawDotnetRoot, &std::free);
            const char* dotnetRootValue = dotnetRoot ? dotnetRoot.get() : nullptr;
            constexpr std::string_view executableName = "dotnet.exe";
#else
            const char* dotnetRoot = std::getenv("DOTNET_ROOT");
            const char* dotnetRootValue = dotnetRoot;
            constexpr std::string_view executableName = "dotnet";
#endif
            if (dotnetRootValue)
            {
                const auto candidate = std::filesystem::path(dotnetRootValue) / executableName;
                if (std::filesystem::is_regular_file(candidate) && HasDotnet10Sdk(candidate))
                    return std::filesystem::absolute(candidate).lexically_normal();
            }

#if defined(_WIN32)
            char* rawEnvironment = nullptr;
            std::size_t environmentSize = 0;
            if (_dupenv_s(&rawEnvironment, &environmentSize, "PATH") != 0)
                rawEnvironment = nullptr;
            const std::unique_ptr<char, decltype(&std::free)> environment(rawEnvironment, &std::free);
            const std::string paths = environment ? std::string(environment.get()) : std::string{};
#else
            const char* environment = std::getenv("PATH");
            const std::string paths = environment ? std::string(environment) : std::string{};
#endif
            if (paths.empty())
                throw std::runtime_error("dotnet was not found because PATH is unavailable.");
#if defined(_WIN32)
            constexpr char separator = ';';
            const std::filesystem::path executable = executableName;
#else
            constexpr char separator = ':';
            const std::filesystem::path executable = executableName;
#endif
            std::size_t begin = 0;
            while (begin <= paths.size())
            {
                const auto end = paths.find(separator, begin);
                const auto candidate = std::filesystem::path(paths.substr(begin, end - begin)) / executable;
                if (std::filesystem::is_regular_file(candidate) && HasDotnet10Sdk(candidate))
                    return std::filesystem::absolute(candidate).lexically_normal();
                if (end == std::string::npos)
                    break;
                begin = end + 1;
            }
            throw std::runtime_error("The .NET 10 SDK was not found on PATH.");
        }

        [[nodiscard]] std::vector<ManagedBuildDiagnostic> ParseDiagnostics(const std::string& output,
                                                                           const std::size_t maximum)
        {
            static const std::regex pattern(
                R"(^(.+?)\(([0-9]+),([0-9]+)\): (warning|error) ([A-Za-z]+[0-9]+): (.+?)(?: \[.+\])?\r?$)");
            std::vector<ManagedBuildDiagnostic> result;
            std::size_t begin = 0;
            while (begin < output.size() && result.size() < maximum)
            {
                const auto end = output.find('\n', begin);
                const std::string line = output.substr(begin, end - begin);
                std::smatch match;
                if (std::regex_match(line, match, pattern))
                {
                    result.push_back(
                        {match[4] == "warning" ? ManagedDiagnosticSeverity::Warning : ManagedDiagnosticSeverity::Error,
                         std::filesystem::path(match[1].str()), static_cast<std::uint32_t>(std::stoul(match[2].str())),
                         static_cast<std::uint32_t>(std::stoul(match[3].str())), match[5].str(), match[6].str()});
                }
                if (end == std::string::npos)
                    break;
                begin = end + 1;
            }
            return result;
        }

        [[nodiscard]] std::string GenerateProject(const ManagedAssemblyGraphEntry& assembly,
                                                  const std::map<AssetId, std::string>& names,
                                                  const std::filesystem::path& projectRoot,
                                                  const std::filesystem::path& managedApi)
        {
            std::string text = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                               "<Project Sdk=\"Microsoft.NET.Sdk\">\n  <PropertyGroup>\n    "
                               "<TargetFramework>net8.0</TargetFramework>\n"
                               "    <ImplicitUsings>enable</ImplicitUsings>\n    <Nullable>enable</Nullable>\n"
                               "    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>\n    <AssemblyName>" +
                               XmlEscape(assembly.Definition.Name) + "</AssemblyName>\n    <RootNamespace>" +
                               XmlEscape(assembly.Definition.RootNamespace) +
                               "</RootNamespace>\n  </PropertyGroup>\n  <ItemGroup>\n";
            for (const auto& root : assembly.Definition.SourceRoots)
            {
                text += "    <Compile Include=\"" + XmlEscape(PathText(root / "**" / "*.cs")) + "\" LinkBase=\"" +
                        XmlEscape(PathText(root)) + "\" />\n";
            }
            text += "  </ItemGroup>\n";
            if (!managedApi.empty())
            {
                std::error_code error;
                auto referencePath = std::filesystem::relative(managedApi, projectRoot, error);
                if (error || referencePath.empty())
                    referencePath = managedApi;
                text += "  <ItemGroup>\n    <Reference Include=\"Keire.Managed\">\n      <HintPath>" +
                        XmlEscape(PathText(referencePath)) +
                        "</HintPath>\n      <Private>false</Private>\n    </Reference>\n  </ItemGroup>\n";
            }
            if (!assembly.Definition.References.empty())
            {
                text += "  <ItemGroup>\n";
                for (const auto reference : assembly.Definition.References)
                    text += "    <ProjectReference Include=\"" + XmlEscape(names.at(reference)) + ".csproj\" />\n";
                text += "  </ItemGroup>\n";
            }
            text += "</Project>\n";
            return text;
        }

        [[nodiscard]] std::string GenerateSolution(const ManagedBuildRequest& request,
                                                   const std::map<AssetId, std::string>& names)
        {
            constexpr std::string_view csharpProject = "{FAE04EC0-301F-11D3-BF4B-00C04F79EFBC}";
            std::string text = "Microsoft Visual Studio Solution File, Format Version 12.00\n"
                               "# Visual Studio Version 17\n"
                               "VisualStudioVersion = 17.0.31903.59\n"
                               "MinimumVisualStudioVersion = 10.0.40219.1\n"
                               "# Generated by Keire Editor\n";
            for (const auto& assembly : request.Assemblies)
            {
                const auto guid = "{" + assembly.Asset.ToString() + "}";
                text += "Project(\"" + std::string(csharpProject) + "\") = \"" + names.at(assembly.Asset) + "\", \"" +
                        names.at(assembly.Asset) + ".csproj\", \"" + guid + "\"\nEndProject\n";
            }
            text += "Global\n"
                    "\tGlobalSection(SolutionConfigurationPlatforms) = preSolution\n"
                    "\t\tDebug|Any CPU = Debug|Any CPU\n"
                    "\t\tRelease|Any CPU = Release|Any CPU\n"
                    "\tEndGlobalSection\n"
                    "\tGlobalSection(ProjectConfigurationPlatforms) = postSolution\n";
            for (const auto& assembly : request.Assemblies)
            {
                const auto guid = "{" + assembly.Asset.ToString() + "}";
                text += "\t\t" + guid + ".Debug|Any CPU.ActiveCfg = Debug|Any CPU\n";
                text += "\t\t" + guid + ".Debug|Any CPU.Build.0 = Debug|Any CPU\n";
                text += "\t\t" + guid + ".Release|Any CPU.ActiveCfg = Release|Any CPU\n";
                text += "\t\t" + guid + ".Release|Any CPU.Build.0 = Release|Any CPU\n";
            }
            text += "\tEndGlobalSection\n"
                    "\tGlobalSection(SolutionProperties) = preSolution\n"
                    "\t\tHideSolutionNode = FALSE\n"
                    "\tEndGlobalSection\n"
                    "EndGlobal\n";
            return text;
        }
    } // namespace

    class ScriptSystem::Impl final
    {
      public:
        class ManagedComponent;

        struct BehaviourType final
        {
            std::string Name;
            ComponentTypeId ComponentType;
            std::int32_t ExecutionOrder = 0;
            const Coral::Type* Type = nullptr;
        };

        struct BehaviourInstance final
        {
            std::string TypeName;
            std::uint64_t World = 0;
            AssetId Entity;
            Coral::ManagedObject Object;
        };

        explicit Impl(ScriptSystemSpecification value)
            : Specification(std::move(value)), Owner(std::this_thread::get_id()),
              Lifetime(std::make_shared<Impl*>(this))
        {
        }

        ~Impl()
        {
            *Lifetime = nullptr;
            StopWorker();
            ShutdownRuntime();
        }

        void RequireOwner() const
        {
            if (std::this_thread::get_id() != Owner)
                throw std::logic_error("ScriptSystem operation must run on the owner thread.");
        }

        void StopWorker() noexcept
        {
            if (Worker.joinable())
            {
                Worker.request_stop();
                Worker.join();
            }
        }

        void InitializeRuntime()
        {
            if (Specification.RuntimeHostDirectory.empty())
                return;
            const auto directory = std::filesystem::absolute(Specification.RuntimeHostDirectory).lexically_normal();
            if (!std::filesystem::is_directory(directory))
                throw std::invalid_argument("The managed runtime host directory does not exist.");
            Coral::HostSettings settings;
            settings.CoralDirectory = PathText(directory);
            if (!Specification.RuntimeRootDirectory.empty())
            {
                const auto runtimeRoot =
                    std::filesystem::absolute(Specification.RuntimeRootDirectory).lexically_normal();
                if (!std::filesystem::is_directory(runtimeRoot))
                    throw std::invalid_argument("The bundled .NET runtime root directory does not exist.");
                settings.DotnetRoot = PathText(runtimeRoot);
            }
            settings.ExceptionCallback = [this](const std::string_view message)
            {
                std::scoped_lock lock(Mutex);
                RuntimeException.assign(message);
            };
            const auto status = RuntimeHost.Initialize(std::move(settings));
            if (status != Coral::CoralInitStatus::Success)
                throw std::runtime_error("Coral could not initialize the bundled .NET 10 runtime host (status " +
                                         std::to_string(static_cast<int>(status)) + ").");
            RuntimeInitialized = true;
        }

        void Unload(std::unique_ptr<Coral::AssemblyLoadContext>& context) noexcept
        {
            if (!context || !RuntimeInitialized)
                return;
            try
            {
                RuntimeHost.UnloadAssemblyLoadContext(*context);
            }
            catch (...)
            {
            }
            context.reset();
        }

        void ShutdownRuntime() noexcept
        {
            Instances.clear();
            ActiveTypes.clear();
            CandidateTypes.clear();
            Unload(CandidateContext);
            Unload(ActiveContext);
            if (RuntimeInitialized)
            {
                try
                {
                    RuntimeHost.Shutdown();
                }
                catch (...)
                {
                }
                RuntimeInitialized = false;
            }
        }

        void ClearRuntimeException()
        {
            std::scoped_lock lock(Mutex);
            RuntimeException.clear();
        }

        void ThrowRuntimeException()
        {
            std::string exception;
            {
                std::scoped_lock lock(Mutex);
                exception = std::exchange(RuntimeException, {});
            }
            if (!exception.empty())
                throw std::runtime_error(exception);
        }

        void Invoke(Coral::ManagedObject& object, const std::string_view method)
        {
            ClearRuntimeException();
            object.InvokeMethod(method);
            ThrowRuntimeException();
        }

        void Invoke(Coral::ManagedObject& object, const std::string_view method, const float value)
        {
            ClearRuntimeException();
            object.InvokeMethod(method, value);
            ThrowRuntimeException();
        }

        [[nodiscard]] Coral::ManagedObject CreateObject(const BehaviourType& type, const std::uint64_t world,
                                                        const AssetId entity)
        {
            ClearRuntimeException();
            auto object = type.Type->CreateInstance();
            ThrowRuntimeException();
            if (!object.IsValid())
                throw std::runtime_error("Managed Behaviour construction returned an invalid object.");
            ClearRuntimeException();
            object.InvokeMethod("RuntimeAttach", world, entity.High(), entity.Low());
            ThrowRuntimeException();
            return object;
        }

        [[nodiscard]] const BehaviourType* FindType(const std::vector<BehaviourType>& types,
                                                    const std::string_view name) const
        {
            const auto found = std::ranges::find(types, name, &BehaviourType::Name);
            return found == types.end() ? nullptr : std::addressof(*found);
        }

        void SetState(const ManagedBuildState state)
        {
            {
                std::scoped_lock lock(Mutex);
                Status.State = state;
            }
            StatusChanged.notify_all();
        }

        void RunBuild(const std::stop_token cancellation, ManagedBuildRequest request,
                      const ManagedBuildOperationId operation) noexcept
        {
            const auto staging = OutputRoot / (".staging-" + std::to_string(operation.Value()));
            try
            {
                SetState(ManagedBuildState::Generating);
                std::error_code error;
                std::filesystem::remove_all(staging, error);
                if (error)
                    throw std::filesystem::filesystem_error("Could not clear managed build staging directory.", staging,
                                                            error);
                std::filesystem::create_directories(staging);

                std::map<AssetId, std::string> names;
                for (const auto& assembly : request.Assemblies)
                    names.emplace(assembly.Asset, assembly.Definition.Name);
                for (const auto& assembly : request.Assemblies)
                    WriteText(staging / (assembly.Definition.Name + ".csproj"),
                              GenerateProject(assembly, names, ProjectRoot, ManagedApi));

                std::string aggregator = "<Project Sdk=\"Microsoft.NET.Sdk\">\n  <PropertyGroup>\n"
                                         "    <TargetFramework>net8.0</TargetFramework>\n"
                                         "    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>\n"
                                         "  </PropertyGroup>\n  <ItemGroup>\n";
                for (const auto& assembly : request.Assemblies)
                    aggregator +=
                        "    <ProjectReference Include=\"" + XmlEscape(assembly.Definition.Name) + ".csproj\" />\n";
                aggregator += "  </ItemGroup>\n</Project>\n";
                const auto aggregatorPath = staging / "Keire.Managed.Build.csproj";
                WriteText(aggregatorPath, aggregator);
                if (cancellation.stop_requested())
                    throw ManagedBuildState::Cancelled;

                SetState(ManagedBuildState::Compiling);
                const std::vector<std::string> arguments{
                    "build",    PathText(aggregatorPath),        "--configuration", request.Configuration, "--nologo",
                    "--output", PathText(staging / "Assemblies")};
                auto process = Detail::ChildProcess::Start(Dotnet, arguments, staging);
                while (!process.Poll())
                {
                    if (cancellation.stop_requested())
                    {
                        process.Terminate();
                        throw ManagedBuildState::Cancelled;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                const auto output = process.TakeOutput();
                auto diagnostics = ParseDiagnostics(output, Specification.MaximumDiagnostics);
                if (process.ExitCode().value_or(1) != 0)
                {
                    if (diagnostics.empty())
                        diagnostics.push_back({ManagedDiagnosticSeverity::Error,
                                               {},
                                               0,
                                               0,
                                               "KEIRECS0001",
                                               output.empty() ? "Managed compilation failed without output." : output});
                    {
                        std::scoped_lock lock(Mutex);
                        Status.Diagnostics = std::move(diagnostics);
                        Status.State = ManagedBuildState::Failed;
                    }
                    StatusChanged.notify_all();
                    std::filesystem::remove_all(staging, error);
                    return;
                }

                SetState(ManagedBuildState::Publishing);
                const auto active = OutputRoot / "Active";
                const auto previous = OutputRoot / ".previous";
                std::filesystem::remove_all(previous, error);
                error.clear();
                if (std::filesystem::exists(active))
                    std::filesystem::rename(active, previous);
                try
                {
                    std::filesystem::rename(staging, active);
                }
                catch (...)
                {
                    if (std::filesystem::exists(previous) && !std::filesystem::exists(active))
                        std::filesystem::rename(previous, active);
                    throw;
                }
                std::filesystem::remove_all(previous, error);
                {
                    std::scoped_lock lock(Mutex);
                    Status.Diagnostics = std::move(diagnostics);
                    Status.ActiveAssemblyDirectory = active / "Assemblies";
                    Status.State = ManagedBuildState::Succeeded;
                }
                StatusChanged.notify_all();
            }
            catch (const ManagedBuildState state)
            {
                std::error_code error;
                std::filesystem::remove_all(staging, error);
                SetState(state);
            }
            catch (const std::exception& exception)
            {
                std::error_code error;
                std::filesystem::remove_all(staging, error);
                {
                    std::scoped_lock lock(Mutex);
                    Status.Diagnostics = {
                        {ManagedDiagnosticSeverity::Error, {}, 0, 0, "KEIRECS0002", exception.what()}};
                    Status.State = ManagedBuildState::Failed;
                }
                StatusChanged.notify_all();
            }
        }

        ScriptSystemSpecification Specification;
        std::thread::id Owner;
        std::atomic<bool> Open{true};
        std::filesystem::path ProjectRoot;
        std::filesystem::path OutputRoot;
        std::filesystem::path Dotnet;
        std::filesystem::path ManagedApi;
        mutable std::mutex Mutex;
        mutable std::condition_variable StatusChanged;
        ManagedBuildStatus Status;
        Coral::HostInstance RuntimeHost;
        std::unique_ptr<Coral::AssemblyLoadContext> ActiveContext;
        std::unique_ptr<Coral::AssemblyLoadContext> CandidateContext;
        std::vector<BehaviourType> ActiveTypes;
        std::vector<BehaviourType> CandidateTypes;
        std::unordered_map<std::uint64_t, BehaviourInstance> Instances;
        std::vector<ComponentTypeId> InstalledComponentTypes;
        std::uint64_t NextInstance = 1;
        std::shared_ptr<Impl*> Lifetime;
        ManagedReloadStatus Reload;
        std::string RuntimeException;
        bool RuntimeInitialized = false;
        std::uint64_t NextReload = 1;
        std::jthread Worker;
        std::uint64_t NextOperation = 1;
    };

    class ScriptSystem::Impl::ManagedComponent final : public Component
    {
      public:
        using ScriptImpl = ScriptSystem::Impl;

        ManagedComponent(const ComponentTypeId componentType, std::string managedType,
                         std::weak_ptr<ScriptImpl*> lifetime)
            : Component(componentType), m_ManagedType(std::move(managedType)), m_Lifetime(std::move(lifetime))
        {
        }

      protected:
        void Awake() override
        {
            WithImplementation(
                [&](ScriptImpl& implementation)
                {
                    const auto* type = implementation.FindType(implementation.ActiveTypes, m_ManagedType);
                    if (!type)
                        return;
                    const auto owner = Owner();
                    const auto id = implementation.NextInstance++;
                    auto object = implementation.CreateObject(*type, 1, owner.Id().Value());
                    implementation.Instances.emplace(
                        id, BehaviourInstance{m_ManagedType, 1, owner.Id().Value(), std::move(object)});
                    m_Instance = ManagedBehaviourInstanceId(id);
                    implementation.Invoke(implementation.Instances.at(id).Object, "RuntimeAwake");
                });
        }

        void OnEnable() override { Invoke(ManagedBehaviourCallback::Enable); }
        void Start() override { Invoke(ManagedBehaviourCallback::Start); }
        void FixedUpdate(const float deltaSeconds) override
        {
            Invoke(ManagedBehaviourCallback::FixedUpdate, deltaSeconds);
        }
        void Update(const float deltaSeconds) override
        {
            Invoke(ManagedBehaviourCallback::Update, deltaSeconds);
            Invoke(ManagedBehaviourCallback::LateUpdate);
        }
        void OnDisable() override { Invoke(ManagedBehaviourCallback::Disable); }
        void OnDestroy() override
        {
            WithImplementation(
                [&](ScriptImpl& implementation)
                {
                    if (!m_Instance)
                        return;
                    const auto found = implementation.Instances.find(m_Instance.Value());
                    if (found == implementation.Instances.end())
                        return;
                    std::exception_ptr failure;
                    try
                    {
                        if (found->second.Object.IsValid())
                            implementation.Invoke(found->second.Object, "RuntimeDestroy");
                    }
                    catch (...)
                    {
                        failure = std::current_exception();
                    }
                    implementation.Instances.erase(found);
                    m_Instance = {};
                    if (failure)
                        std::rethrow_exception(failure);
                });
        }

      private:
        template <typename Function> void WithImplementation(Function&& function)
        {
            const auto lifetime = m_Lifetime.lock();
            if (!lifetime || !*lifetime || !(*lifetime)->Open.load(std::memory_order_acquire))
                return;
            function(**lifetime);
        }

        void Invoke(const ManagedBehaviourCallback callback, const float deltaSeconds = 0.0F)
        {
            WithImplementation(
                [&](ScriptImpl& implementation)
                {
                    if (!m_Instance)
                        return;
                    const auto found = implementation.Instances.find(m_Instance.Value());
                    if (found == implementation.Instances.end() || !found->second.Object.IsValid())
                        return;
                    switch (callback)
                    {
                    case ManagedBehaviourCallback::Enable:
                        implementation.Invoke(found->second.Object, "RuntimeEnable");
                        break;
                    case ManagedBehaviourCallback::Start:
                        implementation.Invoke(found->second.Object, "RuntimeStart");
                        break;
                    case ManagedBehaviourCallback::FixedUpdate:
                        implementation.Invoke(found->second.Object, "RuntimeFixedUpdate", deltaSeconds);
                        break;
                    case ManagedBehaviourCallback::Update:
                        implementation.Invoke(found->second.Object, "RuntimeUpdate", deltaSeconds);
                        break;
                    case ManagedBehaviourCallback::LateUpdate:
                        implementation.Invoke(found->second.Object, "RuntimeLateUpdate");
                        break;
                    default:
                        break;
                    }
                });
        }

        std::string m_ManagedType;
        std::weak_ptr<ScriptImpl*> m_Lifetime;
        ManagedBehaviourInstanceId m_Instance;
    };

    ScriptSystem::ScriptSystem(ScriptSystemSpecification specification) : m_Impl(std::make_unique<Impl>(specification))
    {
        if (specification.Mode == ScriptMode::Disabled || specification.ProjectRoot.empty() ||
            specification.AssemblyDirectory.empty() || specification.AssemblyDirectory.is_absolute() ||
            specification.MaximumDiagnostics == 0 || specification.MaximumDiagnostics > 65536)
            throw std::invalid_argument("ScriptSystem specification is invalid.");
        m_Impl->ProjectRoot = std::filesystem::absolute(specification.ProjectRoot).lexically_normal();
        if (!std::filesystem::is_directory(m_Impl->ProjectRoot))
            throw std::invalid_argument("ScriptSystem project root does not exist.");
        m_Impl->OutputRoot = (m_Impl->ProjectRoot / specification.AssemblyDirectory).lexically_normal();
        if (!specification.ManagedApiAssembly.empty())
        {
            m_Impl->ManagedApi = std::filesystem::absolute(specification.ManagedApiAssembly).lexically_normal();
            if (!std::filesystem::is_regular_file(m_Impl->ManagedApi))
                throw std::invalid_argument("The Keire.Managed API assembly does not exist.");
        }
        m_Impl->Dotnet = specification.DotnetExecutable.empty() ? std::filesystem::path{}
                                                                : ResolveDotnet(specification.DotnetExecutable);
        m_Impl->InitializeRuntime();
    }

    ScriptSystem::~ScriptSystem() = default;
    bool ScriptSystem::IsOpen() const noexcept { return m_Impl->Open.load(std::memory_order_acquire); }

    ManagedIdeWorkspace ScriptSystem::GenerateIdeWorkspace(const ManagedBuildRequest& request,
                                                           const std::string_view solutionName)
    {
        m_Impl->RequireOwner();
        if (!IsOpen())
            throw std::logic_error("ScriptSystem is closed.");
        ValidateManagedAssemblyGraph(request.Assemblies);
        if (request.Assemblies.empty())
            throw std::invalid_argument("IDE workspace generation requires at least one managed assembly.");

        std::string safeName(solutionName);
        for (char& character : safeName)
            if (!std::isalnum(static_cast<unsigned char>(character)) && character != '_' && character != '-')
                character = '_';
        if (safeName.empty())
            safeName = "Game";
        std::map<AssetId, std::string> names;
        for (const auto& assembly : request.Assemblies)
            names.emplace(assembly.Asset, assembly.Definition.Name);

        ManagedIdeWorkspace result;
        result.Solution = m_Impl->ProjectRoot / (safeName + ".sln");
        auto ideManagedApi = m_Impl->ManagedApi;
        if (!ideManagedApi.empty())
        {
            const auto referenceDirectory = m_Impl->ProjectRoot / "Library/ScriptAssemblies/References";
            const auto reference = referenceDirectory / ideManagedApi.filename();
            const auto contents = Detail::ReadTextFile(ideManagedApi, 64U * 1024U * 1024U);
            Detail::WriteFileAtomically(reference, std::as_bytes(std::span(contents)));
            ideManagedApi = reference;
        }
        for (const auto& assembly : request.Assemblies)
        {
            const auto project = m_Impl->ProjectRoot / (assembly.Definition.Name + ".csproj");
            Detail::WriteTextFileAtomically(project,
                                            GenerateProject(assembly, names, m_Impl->ProjectRoot, ideManagedApi));
            result.Projects.push_back(project);
        }
        Detail::WriteTextFileAtomically(result.Solution, GenerateSolution(request, names));
        return result;
    }

    ManagedBuildOperationId ScriptSystem::StartBuild(ManagedBuildRequest request)
    {
        m_Impl->RequireOwner();
        if (!IsOpen())
            throw std::logic_error("ScriptSystem is closed.");
        if (request.Configuration != "Debug" && request.Configuration != "Release")
            throw std::invalid_argument("Managed build configuration must be Debug or Release.");
        ValidateManagedAssemblyGraph(request.Assemblies);
        if (request.Assemblies.empty())
            throw std::invalid_argument("Managed build requires at least one assembly.");
        if (m_Impl->Worker.joinable())
        {
            const auto state = BuildStatus().State;
            if (state == ManagedBuildState::Generating || state == ManagedBuildState::Compiling ||
                state == ManagedBuildState::Publishing)
                throw std::logic_error("A managed build is already running.");
            m_Impl->Worker.join();
        }
        if (m_Impl->Dotnet.empty())
            m_Impl->Dotnet = ResolveDotnet({});

        const ManagedBuildOperationId operation(m_Impl->NextOperation++);
        {
            std::scoped_lock lock(m_Impl->Mutex);
            m_Impl->Status = {.Operation = operation, .State = ManagedBuildState::Generating};
        }
        m_Impl->Worker = std::jthread([implementation = m_Impl.get(), request = std::move(request),
                                       operation](const std::stop_token cancellation) mutable
                                      { implementation->RunBuild(cancellation, std::move(request), operation); });
        return operation;
    }

    void ScriptSystem::CancelBuild(const ManagedBuildOperationId operation)
    {
        m_Impl->RequireOwner();
        if (!operation || BuildStatus().Operation != operation)
            throw std::invalid_argument("Managed build operation is unavailable.");
        if (m_Impl->Worker.joinable())
            m_Impl->Worker.request_stop();
    }

    bool ScriptSystem::WaitForBuild(const ManagedBuildOperationId operation,
                                    const std::chrono::milliseconds timeout) const
    {
        if (!operation || timeout.count() < 0)
            throw std::invalid_argument("Managed build wait operation or timeout is invalid.");
        std::unique_lock lock(m_Impl->Mutex);
        if (m_Impl->Status.Operation != operation)
            throw std::invalid_argument("Managed build operation is unavailable.");
        return m_Impl->StatusChanged.wait_for(lock, timeout,
                                              [this, operation]
                                              {
                                                  if (m_Impl->Status.Operation != operation)
                                                      return true;
                                                  const auto state = m_Impl->Status.State;
                                                  return state == ManagedBuildState::Succeeded ||
                                                         state == ManagedBuildState::Failed ||
                                                         state == ManagedBuildState::Cancelled;
                                              });
    }

    ManagedBuildStatus ScriptSystem::BuildStatus() const
    {
        std::scoped_lock lock(m_Impl->Mutex);
        return m_Impl->Status;
    }

    bool ScriptSystem::RuntimeHostAvailable() const noexcept { return IsOpen() && m_Impl->RuntimeInitialized; }

    bool ScriptSystem::PrepareReload(ManagedReloadRequest request)
    {
        m_Impl->RequireOwner();
        if (!IsOpen())
            throw std::logic_error("ScriptSystem is closed.");
        if (!m_Impl->RuntimeInitialized)
            throw std::logic_error("The managed runtime host is unavailable.");
        if (request.Assemblies.empty())
            throw std::invalid_argument("Managed reload requires at least one assembly.");

        m_Impl->Unload(m_Impl->CandidateContext);
        {
            std::scoped_lock lock(m_Impl->Mutex);
            m_Impl->Reload.State = ManagedReloadState::Preparing;
            m_Impl->Reload.Diagnostic.clear();
            m_Impl->Reload.RetainedState = std::move(request.State);
            m_Impl->RuntimeException.clear();
        }

        try
        {
            auto candidate = m_Impl->RuntimeHost.CreateAssemblyLoadContext(
                "Keire.Reload." + std::to_string(m_Impl->NextReload++), PathText(m_Impl->OutputRoot));
            m_Impl->CandidateContext = std::make_unique<Coral::AssemblyLoadContext>(candidate);
            Coral::Type* behaviourType = nullptr;
            Coral::Type* stableComponentIdType = nullptr;
            Coral::Type* executionOrderType = nullptr;
            if (!m_Impl->ManagedApi.empty())
            {
                const auto& managedApi = m_Impl->CandidateContext->LoadAssembly(PathText(m_Impl->ManagedApi));
                if (managedApi.GetLoadStatus() != Coral::AssemblyLoadStatus::Success)
                    throw std::runtime_error("Managed reload rejected Keire.Managed (status " +
                                             std::to_string(static_cast<int>(managedApi.GetLoadStatus())) + ").");
                behaviourType = &managedApi.GetLocalType("Keire.Behaviour");
                if (!*behaviourType)
                    throw std::runtime_error("Keire.Managed does not expose Keire.Behaviour.");
                stableComponentIdType = &managedApi.GetLocalType("Keire.StableComponentIdAttribute");
                executionOrderType = &managedApi.GetLocalType("Keire.ExecutionOrderAttribute");
                if (!*stableComponentIdType || !*executionOrderType)
                    throw std::runtime_error("Keire.Managed does not expose managed component metadata.");
            }
            std::vector<std::string> availableTypes;
            std::vector<Impl::BehaviourType> candidateTypes;
            for (auto path : request.Assemblies)
            {
                if (path.is_relative())
                    path = m_Impl->ProjectRoot / path;
                path = std::filesystem::absolute(path).lexically_normal();
                if (!std::filesystem::is_regular_file(path))
                    throw std::runtime_error("Managed reload assembly does not exist: " + PathText(path));
                const auto& assembly = m_Impl->CandidateContext->LoadAssembly(PathText(path));
                if (assembly.GetLoadStatus() != Coral::AssemblyLoadStatus::Success)
                    throw std::runtime_error("Managed reload rejected assembly '" + PathText(path) + "' (status " +
                                             std::to_string(static_cast<int>(assembly.GetLoadStatus())) + ").");
                if (behaviourType)
                {
                    for (const auto& type : assembly.GetLocalTypes())
                    {
                        if (!type || !type.IsSubclassOf(*behaviourType))
                            continue;
                        const Coral::ScopedString name(type.GetFullName());
                        const auto typeName = static_cast<std::string>(name);
                        availableTypes.push_back(typeName);

                        ComponentTypeId componentType;
                        std::int32_t executionOrder = 0;
                        for (auto attribute : type.GetAttributes())
                        {
                            if (attribute.GetType() == *stableComponentIdType)
                            {
                                componentType = ComponentTypeId(AssetId(attribute.GetFieldValue<std::uint64_t>("High"),
                                                                        attribute.GetFieldValue<std::uint64_t>("Low")));
                            }
                            else if (attribute.GetType() == *executionOrderType)
                            {
                                executionOrder = attribute.GetFieldValue<std::int32_t>("Order");
                            }
                        }
                        if (componentType)
                            candidateTypes.push_back({typeName, componentType, executionOrder, std::addressof(type)});
                    }
                }
            }
            std::ranges::sort(availableTypes);
            if (std::adjacent_find(availableTypes.begin(), availableTypes.end()) != availableTypes.end())
                throw std::runtime_error("Managed reload contains duplicate Behaviour type names.");
            std::ranges::sort(candidateTypes, {}, &Impl::BehaviourType::ComponentType);
            if (std::ranges::adjacent_find(candidateTypes, {}, &Impl::BehaviourType::ComponentType) !=
                candidateTypes.end())
            {
                throw std::runtime_error("Managed reload contains duplicate stable component IDs.");
            }
            std::ranges::sort(candidateTypes, {}, &Impl::BehaviourType::Name);
            std::string runtimeException;
            {
                std::scoped_lock lock(m_Impl->Mutex);
                runtimeException = m_Impl->RuntimeException;
                if (runtimeException.empty())
                {
                    m_Impl->CandidateTypes = std::move(candidateTypes);
                    m_Impl->Reload.AvailableTypes = std::move(availableTypes);
                    m_Impl->Reload.State = ManagedReloadState::Prepared;
                }
            }
            if (!runtimeException.empty())
                throw std::runtime_error(runtimeException);
            return true;
        }
        catch (const std::exception& error)
        {
            m_Impl->CandidateTypes.clear();
            m_Impl->Unload(m_Impl->CandidateContext);
            std::scoped_lock lock(m_Impl->Mutex);
            m_Impl->Reload.State = ManagedReloadState::Failed;
            m_Impl->Reload.Diagnostic = error.what();
            return false;
        }
    }

    void ScriptSystem::CommitReload()
    {
        m_Impl->RequireOwner();
        if (!IsOpen())
            throw std::logic_error("ScriptSystem is closed.");
        {
            std::scoped_lock lock(m_Impl->Mutex);
            if (m_Impl->Reload.State != ManagedReloadState::Prepared || !m_Impl->CandidateContext)
                throw std::logic_error("No prepared managed reload is available.");
        }

        std::unordered_map<std::uint64_t, Impl::BehaviourInstance> migrated;
        migrated.reserve(m_Impl->Instances.size());
        for (auto& [id, instance] : m_Impl->Instances)
        {
            if (instance.Object.IsValid())
                m_Impl->Invoke(instance.Object, "RuntimeBeforeReload");
            Impl::BehaviourInstance replacement{instance.TypeName, instance.World, instance.Entity, {}};
            if (const auto* type = m_Impl->FindType(m_Impl->CandidateTypes, instance.TypeName))
            {
                replacement.Object = m_Impl->CreateObject(*type, instance.World, instance.Entity);
                m_Impl->Invoke(replacement.Object, "RuntimeAfterReload");
            }
            migrated.emplace(id, std::move(replacement));
        }

        auto previous = std::move(m_Impl->ActiveContext);
        m_Impl->Instances = std::move(migrated);
        m_Impl->ActiveContext = std::move(m_Impl->CandidateContext);
        m_Impl->ActiveTypes = std::move(m_Impl->CandidateTypes);
        m_Impl->Unload(previous);
        std::scoped_lock lock(m_Impl->Mutex);
        ++m_Impl->Reload.Generation;
        m_Impl->Reload.State = ManagedReloadState::Active;
        m_Impl->Reload.Diagnostic.clear();
    }

    void ScriptSystem::CancelReload()
    {
        m_Impl->RequireOwner();
        m_Impl->CandidateTypes.clear();
        m_Impl->Unload(m_Impl->CandidateContext);
        std::scoped_lock lock(m_Impl->Mutex);
        if (m_Impl->Reload.State == ManagedReloadState::Preparing ||
            m_Impl->Reload.State == ManagedReloadState::Prepared)
            m_Impl->Reload.State = ManagedReloadState::Cancelled;
    }

    ManagedReloadStatus ScriptSystem::ReloadStatus() const
    {
        std::scoped_lock lock(m_Impl->Mutex);
        return m_Impl->Reload;
    }

    ManagedBehaviourInstanceId ScriptSystem::CreateBehaviour(std::string typeName, const std::uint64_t world,
                                                             const AssetId entity)
    {
        m_Impl->RequireOwner();
        if (!IsOpen() || !m_Impl->ActiveContext)
            throw std::logic_error("The managed runtime session is not active.");
        const auto* type = m_Impl->FindType(m_Impl->ActiveTypes, typeName);
        if (!type)
            throw std::invalid_argument("The requested managed Behaviour type is unavailable.");
        const auto id = m_Impl->NextInstance++;
        auto object = m_Impl->CreateObject(*type, world, entity);
        m_Impl->Instances.emplace(id, Impl::BehaviourInstance{std::move(typeName), world, entity, std::move(object)});
        return ManagedBehaviourInstanceId(id);
    }

    void ScriptSystem::InvokeBehaviour(const ManagedBehaviourInstanceId instance,
                                       const ManagedBehaviourCallback callback, const float deltaSeconds)
    {
        m_Impl->RequireOwner();
        if (!IsOpen())
            throw std::logic_error("ScriptSystem is closed.");
        if (!instance)
            throw std::invalid_argument("Managed Behaviour instance ID is invalid.");
        const auto found = m_Impl->Instances.find(instance.Value());
        if (found == m_Impl->Instances.end())
            throw std::invalid_argument("Managed Behaviour instance is unavailable.");
        if (!found->second.Object.IsValid())
            return;

        switch (callback)
        {
        case ManagedBehaviourCallback::Awake:
            m_Impl->Invoke(found->second.Object, "RuntimeAwake");
            break;
        case ManagedBehaviourCallback::Enable:
            m_Impl->Invoke(found->second.Object, "RuntimeEnable");
            break;
        case ManagedBehaviourCallback::Start:
            m_Impl->Invoke(found->second.Object, "RuntimeStart");
            break;
        case ManagedBehaviourCallback::FixedUpdate:
            m_Impl->Invoke(found->second.Object, "RuntimeFixedUpdate", deltaSeconds);
            break;
        case ManagedBehaviourCallback::Update:
            m_Impl->Invoke(found->second.Object, "RuntimeUpdate", deltaSeconds);
            break;
        case ManagedBehaviourCallback::LateUpdate:
            m_Impl->Invoke(found->second.Object, "RuntimeLateUpdate");
            break;
        case ManagedBehaviourCallback::Disable:
            m_Impl->Invoke(found->second.Object, "RuntimeDisable");
            break;
        case ManagedBehaviourCallback::Destroy:
            m_Impl->Invoke(found->second.Object, "RuntimeDestroy");
            break;
        case ManagedBehaviourCallback::BeforeReload:
            m_Impl->Invoke(found->second.Object, "RuntimeBeforeReload");
            break;
        case ManagedBehaviourCallback::AfterReload:
            m_Impl->Invoke(found->second.Object, "RuntimeAfterReload");
            break;
        }
    }

    bool ScriptSystem::DestroyBehaviour(const ManagedBehaviourInstanceId instance)
    {
        m_Impl->RequireOwner();
        if (!IsOpen())
            return false;
        const auto found = m_Impl->Instances.find(instance.Value());
        if (found == m_Impl->Instances.end())
            return false;
        std::exception_ptr failure;
        try
        {
            if (found->second.Object.IsValid())
                m_Impl->Invoke(found->second.Object, "RuntimeDestroy");
        }
        catch (...)
        {
            failure = std::current_exception();
        }
        m_Impl->Instances.erase(found);
        if (failure)
            std::rethrow_exception(failure);
        return true;
    }

    void ScriptSystem::InstallManagedComponents(Ref<ComponentRegistry> registry)
    {
        m_Impl->RequireOwner();
        if (!IsOpen() || !m_Impl->ActiveContext)
            throw std::logic_error("The managed runtime session is not active.");
        if (!registry)
            throw std::invalid_argument("Managed component installation requires a component registry.");

        std::vector<ComponentRegistration> registrations;
        registrations.reserve(m_Impl->ActiveTypes.size());
        std::vector<ComponentTypeId> installed;
        installed.reserve(m_Impl->ActiveTypes.size());
        for (const auto& type : m_Impl->ActiveTypes)
        {
            if (!type.ComponentType)
                continue;
            ComponentRegistration registration;
            registration.Type = type.ComponentType;
            registration.Name = type.Name;
            registration.Category = "Scripts";
            registration.ExecutionOrder = type.ExecutionOrder;
            const auto componentType = type.ComponentType;
            const auto managedType = type.Name;
            const std::weak_ptr<Impl*> lifetime = m_Impl->Lifetime;
            registration.Factory = [componentType, managedType, lifetime]
            { return Ref<Component>(CreateRef<Impl::ManagedComponent>(componentType, managedType, lifetime)); };
            registration.Serialize = [](const Component&) { return ComponentPropertyBag{}; };
            registration.Deserialize = [](Component&, const ComponentPropertyBag&, std::uint32_t) {};
            registrations.push_back(std::move(registration));
            installed.push_back(componentType);
        }
        registry->ReplaceBatch(m_Impl->InstalledComponentTypes, std::move(registrations));
        m_Impl->InstalledComponentTypes = std::move(installed);
    }

    void ScriptSystem::Close()
    {
        m_Impl->RequireOwner();
        if (!m_Impl->Open.exchange(false, std::memory_order_acq_rel))
            return;
        m_Impl->StopWorker();
        m_Impl->ShutdownRuntime();
    }
} // namespace Keire
