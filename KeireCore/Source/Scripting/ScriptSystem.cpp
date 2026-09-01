#include "KeireInternal/Scripting/ScriptSystemInternal.h"

namespace Keire
{
    void ScriptSystem::Impl::SetState(const ManagedBuildState state)
    {
        {
            std::scoped_lock lock(Mutex);
            Status.State = state;
        }
        StatusChanged.notify_all();
    }

    void ScriptSystem::Impl::RunBuild(const std::stop_token& cancellation, const ManagedBuildRequest& request,
                                      const ManagedBuildOperationId operation) noexcept
    {
        const auto staging = OutputRoot / "Generations" / std::to_string(operation.Value());
        const auto projectDirectory = OutputRoot / "Intermediate" / "Projects";
        const auto started = std::chrono::steady_clock::now();
        try
        {
            SetState(ManagedBuildState::Generating);
            std::error_code error;
            std::filesystem::remove_all(staging, error);
            if (error)
                throw std::filesystem::filesystem_error("Could not clear managed build staging directory.", staging,
                                                        error);
            std::filesystem::create_directories(staging);
            std::filesystem::create_directories(projectDirectory);
            WriteText(projectDirectory / "Directory.Build.props",
                      "<Project>\n  <PropertyGroup>\n"
                      "    <BaseIntermediateOutputPath>$(MSBuildThisFileDirectory)..\\"
                      "$(MSBuildProjectName)\\</BaseIntermediateOutputPath>\n"
                      "    <MSBuildProjectExtensionsPath>$(BaseIntermediateOutputPath)</"
                      "MSBuildProjectExtensionsPath>\n"
                      "    <UseSharedCompilation>false</UseSharedCompilation>\n"
                      "  </PropertyGroup>\n</Project>\n");

            const auto managedApiOutput = staging / "ManagedApi";
            std::filesystem::create_directories(managedApiOutput);
            auto generationManagedApi = managedApiOutput / "Keire.Managed.dll";
            const auto managedApiProject = FindManagedApiProject();
            if (!managedApiProject.empty())
            {
                const auto fingerprint = ManagedApiSourceFingerprint(managedApiProject);
                const auto cacheDirectory = OutputRoot / "ManagedApiCache";
                const auto cachedAssembly = cacheDirectory / "Keire.Managed.dll";
                const auto cachedFingerprint = cacheDirectory / "source.fingerprint";
                bool cacheHit = false;
                if (std::filesystem::is_regular_file(cachedAssembly) &&
                    std::filesystem::is_regular_file(cachedFingerprint))
                {
                    cacheHit = Detail::ReadTextFile(cachedFingerprint, std::size_t{1} << 20U) == fingerprint;
                }
                if (cacheHit)
                {
                    std::filesystem::copy_file(cachedAssembly, generationManagedApi,
                                               std::filesystem::copy_options::overwrite_existing);
                }
                else
                {
                    const auto managedApiIntermediate = OutputRoot / "Intermediate" / "ManagedApi";
                    const std::vector<std::string> managedApiArguments{
                        "build",
                        PathText(managedApiProject),
                        "--configuration",
                        "Release",
                        "--nologo",
                        "--disable-build-servers",
                        "/nodeReuse:false",
                        "--output",
                        PathText(managedApiOutput),
                        "--property:UseSharedCompilation=false",
                        "--property:BaseIntermediateOutputPath=" + PathText(managedApiIntermediate) + "/"};
                    auto managedApiProcess = Detail::ChildProcess::Start(Dotnet, managedApiArguments, staging);
                    while (!managedApiProcess.Poll())
                    {
                        if (cancellation.stop_requested())
                        {
                            managedApiProcess.Terminate();
                            throw ManagedBuildState::Cancelled;
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }
                    const auto managedApiBuildOutput = managedApiProcess.TakeOutput();
                    if (managedApiProcess.ExitCode().value_or(1) != 0)
                    {
                        throw std::runtime_error(managedApiBuildOutput.empty()
                                                     ? "Keire.Managed compilation failed without output."
                                                     : managedApiBuildOutput);
                    }
                    if (!std::filesystem::is_regular_file(generationManagedApi))
                        throw std::runtime_error("Keire.Managed compilation published no API assembly.");
                    std::filesystem::create_directories(cacheDirectory);
                    const auto assemblyBytes = Detail::ReadTextFile(generationManagedApi, std::size_t{64} << 20U);
                    Detail::WriteFileAtomically(cachedAssembly, std::as_bytes(std::span(assemblyBytes)));
                    Detail::WriteTextFileAtomically(cachedFingerprint, fingerprint);
                }
            }
            else
            {
                if (ManagedApi.empty() || !std::filesystem::is_regular_file(ManagedApi))
                    throw std::runtime_error("The managed build has no valid Keire.Managed API assembly.");
                std::filesystem::copy_file(ManagedApi, generationManagedApi,
                                           std::filesystem::copy_options::overwrite_existing);
            }

            const auto generationManagedEditorApi = managedApiOutput / "Keire.Editor.Managed.dll";
            if (!ManagedEditorApi.empty())
            {
                if (!std::filesystem::is_regular_file(ManagedEditorApi))
                    throw std::runtime_error("The managed build has no valid Keire.Editor.Managed API assembly.");
                std::filesystem::copy_file(ManagedEditorApi, generationManagedEditorApi,
                                           std::filesystem::copy_options::overwrite_existing);
            }
            const auto generationManagedGenerator = managedApiOutput / "Keire.Managed.Generators.dll";
            if (!ManagedGenerator.empty())
            {
                if (!std::filesystem::is_regular_file(ManagedGenerator))
                    throw std::runtime_error("The managed build has no valid Keire.Managed.Generators analyzer.");
                std::filesystem::copy_file(ManagedGenerator, generationManagedGenerator,
                                           std::filesystem::copy_options::overwrite_existing);
            }

            std::map<AssetId, std::string> names;
            for (const auto& assembly : request.Assemblies)
                names.emplace(assembly.Asset, assembly.Definition.Name);
            for (const auto& assembly : request.Assemblies)
                WriteText(
                    projectDirectory / (assembly.Definition.Name + ".csproj"),
                    GenerateProject(assembly, names, ProjectRoot, projectDirectory, generationManagedApi, {},
                                    ManagedEditorApi.empty() ? std::filesystem::path{} : generationManagedEditorApi,
                                    ManagedGenerator.empty() ? std::filesystem::path{} : generationManagedGenerator,
                                    assembly.Definition.Classification == ManagedAssemblyClassification::Editor,
                                    "net10.0", "14.0"));

            const auto aggregatorPath = projectDirectory / "Keire.Managed.Build.csproj";
            WriteText(aggregatorPath, GenerateManagedBuildAggregator(request));
            if (cancellation.stop_requested())
                throw ManagedBuildState::Cancelled;

            SetState(ManagedBuildState::Compiling);
            const std::vector<std::string> arguments{"build",
                                                     PathText(aggregatorPath),
                                                     "--configuration",
                                                     request.Configuration,
                                                     "--nologo",
                                                     "--disable-build-servers",
                                                     "/nodeReuse:false",
                                                     "--output",
                                                     PathText(staging / "Assemblies"),
                                                     "--property:UseSharedCompilation=false"};
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
            {
                std::scoped_lock lock(Mutex);
                if (cancellation.stop_requested() || Status.Operation != operation)
                    throw ManagedBuildState::Cancelled;
            }
            const auto& active = staging;
            Detail::WriteTextFileAtomically(OutputRoot / "active-generation.json",
                                            "{\"generation\":" + std::to_string(operation.Value()) +
                                                ",\"directory\":\"" +
                                                PathText(std::filesystem::relative(active, OutputRoot)) + "\"}\n");
            {
                std::scoped_lock lock(Mutex);
                Status.Diagnostics = std::move(diagnostics);
                Status.ActiveAssemblyDirectory = active / "Assemblies";
                Status.ManagedApiAssembly = generationManagedApi;
                Status.ManagedEditorApiAssembly =
                    ManagedEditorApi.empty() ? std::filesystem::path{} : generationManagedEditorApi;
                Status.Generation = operation.Value();
                Status.Elapsed =
                    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
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
                Status.Diagnostics = {{ManagedDiagnosticSeverity::Error, {}, 0, 0, "KEIRECS0002", exception.what()}};
                Status.State = ManagedBuildState::Failed;
            }
            StatusChanged.notify_all();
        }
    }

    ScriptSystem::ScriptSystem(ScriptSystemSpecification specification, Ref<JobSystem> jobs)
        : m_Impl(std::make_unique<Impl>(specification, std::move(jobs)))
    {
        if (specification.Mode == ScriptMode::Disabled || specification.ProjectRoot.empty() ||
            specification.AssemblyDirectory.empty() || specification.AssemblyDirectory.is_absolute() ||
            specification.MaximumDiagnostics == 0 || specification.MaximumDiagnostics > 65536)
            throw std::invalid_argument("ScriptSystem specification is invalid.");
        m_Impl->ProjectRoot = std::filesystem::absolute(specification.ProjectRoot).lexically_normal();
        if (!std::filesystem::is_directory(m_Impl->ProjectRoot))
            throw std::invalid_argument("ScriptSystem project root does not exist.");
        const auto sdk = Detail::ReadManagedSdkConfiguration(
            m_Impl->ProjectRoot, {specification.SdkSelection, specification.DotnetExecutable});
        m_Impl->Specification.SdkSelection = sdk.Selection;
        m_Impl->Specification.DotnetExecutable = sdk.CustomExecutable;
        m_Impl->OutputRoot = (m_Impl->ProjectRoot / specification.AssemblyDirectory).lexically_normal();
        m_Impl->ResumeGenerationSequence();
        if (!specification.ManagedApiAssembly.empty())
        {
            m_Impl->ManagedApi = std::filesystem::absolute(specification.ManagedApiAssembly).lexically_normal();
            if (!std::filesystem::is_regular_file(m_Impl->ManagedApi))
                throw std::invalid_argument("The Keire.Managed API assembly does not exist.");
        }
        if (!specification.ManagedEditorApiAssembly.empty())
        {
            m_Impl->ManagedEditorApi =
                std::filesystem::absolute(specification.ManagedEditorApiAssembly).lexically_normal();
            if (!std::filesystem::is_regular_file(m_Impl->ManagedEditorApi))
                throw std::invalid_argument("The Keire.Editor.Managed API assembly does not exist.");
        }
        if (!specification.ManagedGeneratorAssembly.empty())
        {
            m_Impl->ManagedGenerator =
                std::filesystem::absolute(specification.ManagedGeneratorAssembly).lexically_normal();
            if (!std::filesystem::is_regular_file(m_Impl->ManagedGenerator))
                throw std::invalid_argument("The Keire.Managed.Generators analyzer does not exist.");
        }
        m_Impl->Dotnet =
            m_Impl->Specification.DotnetExecutable.empty()
                ? std::filesystem::path{}
                : Detail::ResolveDotnet(m_Impl->Specification.DotnetExecutable, m_Impl->Specification.SdkSelection,
                                        m_Impl->ProjectRoot, m_Impl->Specification.RuntimeRootDirectory);
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
        auto ideManagedEditorApi = m_Impl->ManagedEditorApi;
        auto ideManagedGenerator = m_Impl->ManagedGenerator;
        auto ideManagedApiProject = m_Impl->FindManagedApiProject();
        if (!ideManagedApi.empty())
        {
            const auto referenceDirectory = m_Impl->ProjectRoot / "Library/ScriptAssemblies/References";
            const auto reference = referenceDirectory / ideManagedApi.filename();
            const auto contents = Detail::ReadTextFile(ideManagedApi, std::size_t{64} << 20U);
            (void)Detail::WriteFileAtomicallyIfChanged(reference, std::as_bytes(std::span(contents)));
            ideManagedApi = reference;
            if (!ideManagedEditorApi.empty())
            {
                const auto editorReference = referenceDirectory / ideManagedEditorApi.filename();
                const auto editorContents = Detail::ReadTextFile(ideManagedEditorApi, std::size_t{64} << 20U);
                (void)Detail::WriteFileAtomicallyIfChanged(editorReference, std::as_bytes(std::span(editorContents)));
                ideManagedEditorApi = editorReference;
            }
            if (!ideManagedGenerator.empty())
            {
                const auto generatorReference = referenceDirectory / ideManagedGenerator.filename();
                const auto generatorContents = Detail::ReadTextFile(ideManagedGenerator, std::size_t{64} << 20U);
                (void)Detail::WriteFileAtomicallyIfChanged(generatorReference,
                                                           std::as_bytes(std::span(generatorContents)));
                ideManagedGenerator = generatorReference;
            }
            if (!ideManagedApiProject.empty())
            {
                const auto designTimeProject = referenceDirectory / "Keire.Managed.VisualStudio.csproj";
                (void)Detail::WriteTextFileAtomicallyIfChanged(
                    designTimeProject, GenerateManagedApiDesignTimeProject(ideManagedApiProject, designTimeProject));
                ideManagedApiProject = designTimeProject;
            }
        }
        const bool usesManagedApiDesignTimeProject = !ideManagedApiProject.empty();
        const std::string_view ideTargetFramework = usesManagedApiDesignTimeProject ? "net8.0" : "net10.0";
        const std::string_view ideLanguageVersion = usesManagedApiDesignTimeProject ? "12.0" : "14.0";
        for (const auto& assembly : request.Assemblies)
        {
            const auto project = m_Impl->ProjectRoot / (assembly.Definition.Name + ".csproj");
            (void)Detail::WriteTextFileAtomicallyIfChanged(
                project, GenerateProject(assembly, names, m_Impl->ProjectRoot, m_Impl->ProjectRoot, ideManagedApi,
                                         ideManagedApiProject, ideManagedEditorApi, ideManagedGenerator,
                                         assembly.Definition.Classification == ManagedAssemblyClassification::Editor,
                                         ideTargetFramework, ideLanguageVersion));
            result.Projects.push_back(project);
        }
        (void)Detail::WriteTextFileAtomicallyIfChanged(
            result.Solution, GenerateSolution(request, names, m_Impl->ProjectRoot, ideManagedApiProject));
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
        m_Impl->StopWorker();
        if (m_Impl->Dotnet.empty())
            m_Impl->Dotnet =
                Detail::ResolveDotnet(m_Impl->Specification.DotnetExecutable, m_Impl->Specification.SdkSelection,
                                      m_Impl->Specification.ProjectRoot, m_Impl->Specification.RuntimeRootDirectory);

        const ManagedBuildOperationId operation(m_Impl->NextOperation++);
        {
            std::scoped_lock lock(m_Impl->Mutex);
            m_Impl->Status = {.Operation = operation, .State = ManagedBuildState::Generating};
            for (const auto& assembly : request.Assemblies)
                m_Impl->Status.ChangedAssemblies.push_back(assembly.Definition.Name);
        }
        m_Impl->BuildCancellation = std::stop_source{};
        const auto buildCancellation = m_Impl->BuildCancellation.get_token();
        m_Impl->Worker = m_Impl->WorkScope->Submit(
            {.Name = "Managed assembly build",
             .Priority = JobPriority::High,
             .Class = JobClass::Blocking,
             .Domain = JobDomain::Tooling},
            [implementation = m_Impl.get(), request = std::move(request), operation,
             buildCancellation](JobContext& context)
            {
                std::stop_source combined;
                std::stop_callback schedulerStop(context.StopToken(), [&combined] { combined.request_stop(); });
                std::stop_callback buildStop(buildCancellation, [&combined] { combined.request_stop(); });
                implementation->RunBuild(combined.get_token(), request, operation);
            });
        return operation;
    }

    void ScriptSystem::CancelBuild(const ManagedBuildOperationId operation)
    {
        m_Impl->RequireOwner();
        if (!operation || BuildStatus().Operation != operation)
            throw std::invalid_argument("Managed build operation is unavailable.");
        m_Impl->BuildCancellation.request_stop();
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

    ManagedSdkConfiguration ScriptSystem::SdkConfiguration() const
    {
        m_Impl->RequireOwner();
        return {m_Impl->Specification.SdkSelection, m_Impl->Specification.DotnetExecutable};
    }

    void ScriptSystem::ConfigureManagedSdk(const ManagedSdkSelection selection, std::filesystem::path customExecutable)
    {
        m_Impl->RequireOwner();
        if (selection != ManagedSdkSelection::Custom)
            customExecutable.clear();
        if (m_Impl->Specification.SdkSelection == selection &&
            m_Impl->Specification.DotnetExecutable == customExecutable)
            return;
        const auto state = BuildStatus().State;
        if (state == ManagedBuildState::Generating || state == ManagedBuildState::Compiling ||
            state == ManagedBuildState::Publishing)
            throw std::logic_error("The managed SDK cannot be changed while a script build is active.");
        m_Impl->Specification.SdkSelection = selection;
        m_Impl->Specification.DotnetExecutable = std::move(customExecutable);
        m_Impl->Dotnet.clear();
        Detail::WriteManagedSdkConfiguration(
            m_Impl->ProjectRoot, {m_Impl->Specification.SdkSelection, m_Impl->Specification.DotnetExecutable});
    }

    bool ScriptSystem::RuntimeHostAvailable() const noexcept { return IsOpen() && m_Impl->RuntimeInitialized; }

} // namespace Keire
