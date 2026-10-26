#include "KeireInternal/Process.h"

#include "Keire/Log.h"
#include "KeireInternal/FileSystem.h"
#include "KeireInternal/VisualStudioDiscovery.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <OleAuto.h>
#include <objbase.h>
#include <shellapi.h>

#if defined(_MSC_VER)
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "OleAut32.lib")
#pragma comment(lib, "Shell32.lib")
#endif
#endif

namespace Keire::Detail
{
#if defined(_WIN32)
    namespace
    {
        template <typename Type> class ComReference final
        {
          public:
            ComReference() = default;

            explicit ComReference(Type* value) noexcept : m_Value(value) {}

            ~ComReference()
            {
                if (m_Value)
                {
                    m_Value->Release();
                }
            }

            ComReference(const ComReference&) = delete;
            ComReference& operator=(const ComReference&) = delete;

            ComReference(ComReference&& other) noexcept : m_Value(std::exchange(other.m_Value, nullptr)) {}

            ComReference& operator=(ComReference&& other) noexcept
            {
                if (this == &other)
                {
                    return *this;
                }
                if (m_Value)
                {
                    m_Value->Release();
                }
                m_Value = std::exchange(other.m_Value, nullptr);
                return *this;
            }

            [[nodiscard]] Type* Get() const noexcept { return m_Value; }

            [[nodiscard]] Type** Receive() noexcept { return &m_Value; }

            [[nodiscard]] explicit operator bool() const noexcept { return m_Value != nullptr; }

          private:
            Type* m_Value = nullptr;
        };

        [[nodiscard]] DISPID ResolveDispatchMember(IDispatch& dispatch, wchar_t* name)
        {
            DISPID member = DISPID_UNKNOWN;
            if (FAILED(dispatch.GetIDsOfNames(IID_NULL, &name, 1, LOCALE_USER_DEFAULT, &member)))
            {
                return DISPID_UNKNOWN;
            }
            return member;
        }

        [[nodiscard]] ComReference<IDispatch> ReadDispatchProperty(IDispatch& dispatch, wchar_t* name)
        {
            const auto member = ResolveDispatchMember(dispatch, name);
            if (member == DISPID_UNKNOWN)
            {
                return {};
            }
            DISPPARAMS parameters{};
            VARIANT result;
            VariantInit(&result);
            const auto status = dispatch.Invoke(member, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_PROPERTYGET,
                                                &parameters, &result, nullptr, nullptr);
            if (FAILED(status) || result.vt != VT_DISPATCH || !result.pdispVal)
            {
                VariantClear(&result);
                return {};
            }
            auto* value = result.pdispVal;
            result.vt = VT_EMPTY;
            return ComReference<IDispatch>(value);
        }

        [[nodiscard]] std::wstring ReadStringProperty(IDispatch& dispatch, wchar_t* name)
        {
            const auto member = ResolveDispatchMember(dispatch, name);
            if (member == DISPID_UNKNOWN)
            {
                return {};
            }
            DISPPARAMS parameters{};
            VARIANT result;
            VariantInit(&result);
            const auto status = dispatch.Invoke(member, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_PROPERTYGET,
                                                &parameters, &result, nullptr, nullptr);
            if (FAILED(status) || result.vt != VT_BSTR || !result.bstrVal)
            {
                VariantClear(&result);
                return {};
            }
            std::wstring value(result.bstrVal, SysStringLen(result.bstrVal));
            VariantClear(&result);
            return value;
        }

        [[nodiscard]] ComReference<IDispatch> FindEngineReference(IDispatch& references)
        {
            const auto countMember = ResolveDispatchMember(references, const_cast<wchar_t*>(L"Count"));
            const auto itemMember = ResolveDispatchMember(references, const_cast<wchar_t*>(L"Item"));
            if (countMember == DISPID_UNKNOWN || itemMember == DISPID_UNKNOWN)
            {
                return {};
            }
            DISPPARAMS noArguments{};
            VARIANT count;
            VariantInit(&count);
            const auto status = references.Invoke(countMember, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_PROPERTYGET,
                                                  &noArguments, &count, nullptr, nullptr);
            constexpr LONG maximumReferences = 1024;
            const LONG size = SUCCEEDED(status) && count.vt == VT_I4 ? count.lVal : 0;
            VariantClear(&count);
            if (size < 1 || size > maximumReferences)
            {
                return {};
            }
            // References.Find expects a full Identity, not an assembly's simple Name.
            for (LONG index = 1; index <= size; ++index)
            {
                VARIANTARG argument;
                VariantInit(&argument);
                argument.vt = VT_I4;
                argument.lVal = index;
                DISPPARAMS parameters{&argument, nullptr, 1, 0};
                VARIANT result;
                VariantInit(&result);
                const auto itemStatus =
                    references.Invoke(itemMember, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD | DISPATCH_PROPERTYGET,
                                      &parameters, &result, nullptr, nullptr);
                ComReference<IDispatch> reference;
                if (SUCCEEDED(itemStatus) && result.vt == VT_DISPATCH && result.pdispVal)
                {
                    reference = ComReference<IDispatch>(result.pdispVal);
                    result.vt = VT_EMPTY;
                }
                VariantClear(&result);
                if (!reference)
                {
                    continue;
                }
                const auto name = ReadStringProperty(*reference.Get(), const_cast<wchar_t*>(L"Name"));
                const auto path = ReadStringProperty(*reference.Get(), const_cast<wchar_t*>(L"Path"));
                if (name == L"Keire.Managed" || std::filesystem::path(path).filename() == L"Keire.Managed.dll")
                {
                    return reference;
                }
            }
            return {};
        }

        [[nodiscard]] ComReference<IDispatch> InvokeObjectMethod(IDispatch& dispatch, wchar_t* name,
                                                                 const std::wstring_view value)
        {
            const auto member = ResolveDispatchMember(dispatch, name);
            if (member == DISPID_UNKNOWN)
            {
                return {};
            }
            VARIANTARG argument;
            VariantInit(&argument);
            argument.vt = VT_BSTR;
            argument.bstrVal = SysAllocStringLen(value.data(), static_cast<UINT>(value.size()));
            if (!argument.bstrVal)
            {
                return {};
            }
            DISPPARAMS parameters{&argument, nullptr, 1, 0};
            VARIANT result;
            VariantInit(&result);
            const auto status = dispatch.Invoke(member, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &parameters,
                                                &result, nullptr, nullptr);
            VariantClear(&argument);
            if (FAILED(status) || result.vt != VT_DISPATCH || !result.pdispVal)
            {
                VariantClear(&result);
                return {};
            }
            auto* object = result.pdispVal;
            result.vt = VT_EMPTY;
            return ComReference<IDispatch>(object);
        }

        [[nodiscard]] bool SameWindowsPath(const std::filesystem::path& left, const std::filesystem::path& right)
        {
            std::error_code leftError;
            std::error_code rightError;
            const auto normalizedLeft = std::filesystem::weakly_canonical(left, leftError).wstring();
            const auto normalizedRight = std::filesystem::weakly_canonical(right, rightError).wstring();
            if (leftError || rightError)
            {
                return false;
            }
            return CompareStringOrdinal(normalizedLeft.c_str(), static_cast<int>(normalizedLeft.size()),
                                        normalizedRight.c_str(), static_cast<int>(normalizedRight.size()),
                                        TRUE) == CSTR_EQUAL;
        }

        [[nodiscard]] std::filesystem::path ResolveAssociatedExecutable(const std::filesystem::path& document,
                                                                        const std::filesystem::path& working)
        {
            std::array<wchar_t, 32768> executable{};
            const auto result =
                FindExecutableW(document.wstring().c_str(), working.wstring().c_str(), executable.data());
            if (reinterpret_cast<std::intptr_t>(result) <= 32 || executable.front() == L'\0')
            {
                return {};
            }
            return executable.data();
        }

        [[nodiscard]] std::filesystem::path ResolveLegacyVisualStudioExecutable()
        {
            constexpr auto key = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\devenv.exe";
            const std::array roots{HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE};
            for (const auto root : roots)
            {
                std::array<wchar_t, 32768> executable{};
                DWORD bytes = static_cast<DWORD>(executable.size() * sizeof(wchar_t));
                if (RegGetValueW(root, key, nullptr, RRF_RT_REG_SZ, nullptr, executable.data(), &bytes) ==
                        ERROR_SUCCESS &&
                    executable.front() != L'\0')
                {
                    const std::filesystem::path result = executable.data();
                    if (std::filesystem::is_regular_file(result))
                    {
                        return result;
                    }
                }
            }
            return {};
        }

        [[nodiscard]] bool InvokeVisualStudioOpenFile(IDispatch& dte, const std::filesystem::path& source,
                                                      std::string& diagnostic, const std::uint32_t line,
                                                      const std::uint32_t column)
        {
            if (source.extension() == ".cs")
            {
                auto solution = ReadDispatchProperty(dte, const_cast<wchar_t*>(L"Solution"));
                const auto solutionPath =
                    solution ? ReadStringProperty(*solution.Get(), const_cast<wchar_t*>(L"FullName")) : std::wstring{};
                if (!solution || solutionPath.empty() ||
                    !OpenLoadedVisualStudioProjectItem(*solution.Get(), source, solutionPath, diagnostic))
                {
                    return false;
                }
                if (line == 0)
                {
                    return true;
                }
            }
            if (source.extension() != ".cs")
            {
                auto itemOperations = ReadDispatchProperty(dte, const_cast<wchar_t*>(L"ItemOperations"));
                if (!itemOperations)
                {
                    diagnostic = "The matching Visual Studio instance did not expose ItemOperations.";
                    return false;
                }
                const auto member = ResolveDispatchMember(*itemOperations.Get(), const_cast<wchar_t*>(L"OpenFile"));
                if (member == DISPID_UNKNOWN)
                {
                    diagnostic = "The matching Visual Studio instance did not expose OpenFile.";
                    return false;
                }
                VARIANTARG argument;
                VariantInit(&argument);
                argument.vt = VT_BSTR;
                argument.bstrVal = SysAllocString(source.wstring().c_str());
                if (!argument.bstrVal)
                {
                    diagnostic = "Visual Studio source-path allocation failed.";
                    return false;
                }
                DISPPARAMS parameters{&argument, nullptr, 1, 0};
                VARIANT result;
                VariantInit(&result);
                EXCEPINFO exception{};
                UINT argumentError = 0;
                const auto status = itemOperations.Get()->Invoke(member, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD,
                                                                 &parameters, &result, &exception, &argumentError);
                VariantClear(&argument);
                VariantClear(&result);
                SysFreeString(exception.bstrSource);
                SysFreeString(exception.bstrDescription);
                SysFreeString(exception.bstrHelpFile);
                if (FAILED(status))
                {
                    diagnostic = "Visual Studio could not open the source in the matching solution instance.";
                    return false;
                }
            }
            if (line != 0)
            {
                auto document = ReadDispatchProperty(dte, const_cast<wchar_t*>(L"ActiveDocument"));
                auto selection = document ? ReadDispatchProperty(*document.Get(), const_cast<wchar_t*>(L"Selection"))
                                          : ComReference<IDispatch>{};
                const auto move =
                    selection ? ResolveDispatchMember(*selection.Get(), const_cast<wchar_t*>(L"MoveToLineAndOffset"))
                              : DISPID_UNKNOWN;
                if (move == DISPID_UNKNOWN)
                {
                    diagnostic = "Source opened, but Visual Studio did not expose text navigation.";
                    return false;
                }
                VARIANTARG coordinates[2];
                for (auto& coordinate : coordinates)
                {
                    VariantInit(&coordinate);
                    coordinate.vt = VT_I4;
                }
                coordinates[0].lVal = static_cast<LONG>(std::max(column, 1U));
                coordinates[1].lVal = static_cast<LONG>(line);
                DISPPARAMS position{coordinates, nullptr, 2, 0};
                if (FAILED(selection.Get()->Invoke(move, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &position,
                                                   nullptr, nullptr, nullptr)))
                {
                    diagnostic = "Source opened, but Visual Studio could not navigate to the diagnostic location.";
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool OpenInMatchingVisualStudioInstance(const std::filesystem::path& source,
                                                              const std::filesystem::path& solution,
                                                              const std::filesystem::path& executable,
                                                              const std::uint32_t minimumMajor, bool& matchingInstance,
                                                              std::string& diagnostic, const std::uint32_t line,
                                                              const std::uint32_t column)
        {
            matchingInstance = false;
            const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            const bool uninitialize = initialized == S_OK || initialized == S_FALSE;
            if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE)
            {
                return false;
            }

            ComReference<IRunningObjectTable> table;
            ComReference<IEnumMoniker> enumeration;
            ComReference<IBindCtx> bindContext;
            if (FAILED(GetRunningObjectTable(0, table.Receive())) || !table ||
                FAILED(table.Get()->EnumRunning(enumeration.Receive())) || !enumeration ||
                FAILED(CreateBindCtx(0, bindContext.Receive())) || !bindContext)
            {
                bindContext = {};
                enumeration = {};
                table = {};
                if (uninitialize)
                {
                    CoUninitialize();
                }
                return false;
            }

            bool opened = false;
            for (;;)
            {
                ComReference<IMoniker> moniker;
                ULONG fetched = 0;
                if (enumeration.Get()->Next(1, moniker.Receive(), &fetched) != S_OK || fetched != 1)
                {
                    break;
                }
                LPOLESTR displayName = nullptr;
                if (FAILED(moniker.Get()->GetDisplayName(bindContext.Get(), nullptr, &displayName)) || !displayName)
                {
                    continue;
                }
                const std::wstring_view name(displayName);
                const bool visualStudio = IsCompatibleVisualStudioDteMoniker(name, minimumMajor);
                CoTaskMemFree(displayName);
                if (!visualStudio)
                {
                    continue;
                }

                ComReference<IUnknown> object;
                ComReference<IDispatch> dte;
                if (FAILED(table.Get()->GetObject(moniker.Get(), object.Receive())) || !object ||
                    FAILED(object.Get()->QueryInterface(IID_IDispatch, reinterpret_cast<void**>(dte.Receive()))) ||
                    !dte)
                {
                    continue;
                }
                const auto runningExecutable = ReadStringProperty(*dte.Get(), const_cast<wchar_t*>(L"FullName"));
                if (!SameWindowsPath(runningExecutable, executable) ||
                    !IsVisualStudioExecutableCompatible(runningExecutable, minimumMajor))
                {
                    continue;
                }
                auto loadedSolution = ReadDispatchProperty(*dte.Get(), const_cast<wchar_t*>(L"Solution"));
                if (!loadedSolution)
                {
                    continue;
                }
                const auto fullName = ReadStringProperty(*loadedSolution.Get(), const_cast<wchar_t*>(L"FullName"));
                if (fullName.empty() || !SameWindowsPath(fullName, solution))
                {
                    continue;
                }
                matchingInstance = true;
                opened = InvokeVisualStudioOpenFile(*dte.Get(), source, diagnostic, line, column);
                if (opened)
                {
                    break;
                }
            }
            bindContext = {};
            enumeration = {};
            table = {};
            if (uninitialize)
            {
                CoUninitialize();
            }
            return opened;
        }

        [[nodiscard]] bool WaitForMatchingVisualStudioInstance(const std::filesystem::path& source,
                                                               const std::filesystem::path& solution,
                                                               const std::filesystem::path& executable,
                                                               const std::chrono::milliseconds timeout,
                                                               const std::uint32_t minimumMajor, bool& matchingInstance,
                                                               std::string& diagnostic, const std::uint32_t line = 0,
                                                               const std::uint32_t column = 0)
        {
            const auto deadline = std::chrono::steady_clock::now() + timeout;
            matchingInstance = false;
            do
            {
                bool currentMatch = false;
                std::string currentDiagnostic;
                if (OpenInMatchingVisualStudioInstance(source, solution, executable, minimumMajor, currentMatch,
                                                       currentDiagnostic, line, column))
                {
                    return true;
                }
                matchingInstance = matchingInstance || currentMatch;
                if (!currentDiagnostic.empty())
                {
                    diagnostic = std::move(currentDiagnostic);
                }
                if (std::chrono::steady_clock::now() >= deadline)
                {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            } while (true);
            return false;
        }

        void OpenInVisualStudioWhenSolutionReady(std::filesystem::path source, std::filesystem::path solution,
                                                 std::filesystem::path executable, const std::uint32_t minimumMajor,
                                                 const std::uint32_t line = 0, const std::uint32_t column = 0)
        {
            std::thread(
                [source = std::move(source), solution = std::move(solution), executable = std::move(executable),
                 minimumMajor, line, column]
                {
                    bool matchingInstance = false;
                    std::string ignoredDiagnostic;
                    if (!WaitForMatchingVisualStudioInstance(source, solution, executable, std::chrono::seconds(120),
                                                             minimumMajor, matchingInstance, ignoredDiagnostic, line,
                                                             column))
                    {
                        Log::GetCoreLogger().Write(
                            LogLevel::Warn,
                            "Visual Studio script handoff timed out waiting for its loaded C# project: " +
                                ignoredDiagnostic);
                    }
                })
                .detach();
        }

    } // namespace
#endif

#if defined(_WIN32)
    bool OpenLoadedVisualStudioProjectItem(IDispatch& solution, const std::filesystem::path& source,
                                           const std::filesystem::path& solutionPath, std::string& diagnostic)
    {
        diagnostic = "Waiting for Visual Studio to load the script's C# project and engine reference.";
        auto item = InvokeObjectMethod(solution, const_cast<wchar_t*>(L"FindProjectItem"), source.wstring());
        if (!item)
        {
            diagnostic = "Waiting for Visual Studio to expose the script as a loaded project item.";
            return false;
        }
        auto project = item ? ReadDispatchProperty(*item.Get(), const_cast<wchar_t*>(L"ContainingProject"))
                            : ComReference<IDispatch>{};
        const std::filesystem::path projectPath =
            project ? ReadStringProperty(*project.Get(), const_cast<wchar_t*>(L"FullName")) : std::wstring{};
        if (projectPath.extension() != L".csproj" ||
            !SameWindowsPath(projectPath.parent_path(), solutionPath.parent_path()))
        {
            diagnostic = "Waiting for the script's owning C# project in the requested solution.";
            return false;
        }
        auto languageProject = ReadDispatchProperty(*project.Get(), const_cast<wchar_t*>(L"Object"));
        if (!languageProject)
        {
            diagnostic = "Waiting for the loaded C# project's automation object.";
            return false;
        }
        auto references = languageProject
                              ? ReadDispatchProperty(*languageProject.Get(), const_cast<wchar_t*>(L"References"))
                              : ComReference<IDispatch>{};
        if (!references)
        {
            diagnostic = "Waiting for the C# project's engine reference collection.";
            return false;
        }
        auto engineReference = FindEngineReference(*references.Get());
        if (!engineReference)
        {
            diagnostic = "Waiting for Keire.Managed in the loaded C# project's references.";
            return false;
        }
        const auto resolved = ResolveDispatchMember(*engineReference.Get(), const_cast<wchar_t*>(L"Resolved"));
        DISPPARAMS noArguments{};
        VARIANT result;
        VariantInit(&result);
        const auto status = engineReference.Get()->Invoke(resolved, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_PROPERTYGET,
                                                          &noArguments, &result, nullptr, nullptr);
        const bool ready = SUCCEEDED(status) && result.vt == VT_BOOL && result.boolVal == VARIANT_TRUE;
        VariantClear(&result);
        if (!ready)
        {
            diagnostic = "Waiting for the loaded Keire.Managed reference to resolve (HRESULT " +
                         std::to_string(static_cast<long>(status)) + ").";
            return false;
        }
        // Opening the owned item, not a loose file, gives Roslyn the evaluated project context.
        auto window =
            InvokeObjectMethod(*item.Get(), const_cast<wchar_t*>(L"Open"), L"{7651A700-06E5-11D1-8EBD-00A0C90F26EA}");
        if (!window)
        {
            diagnostic = "Visual Studio could not open the loaded script project item.";
            return false;
        }
        const auto activate = ResolveDispatchMember(*window.Get(), const_cast<wchar_t*>(L"Activate"));
        if (FAILED(window.Get()->Invoke(activate, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &noArguments, nullptr,
                                        nullptr, nullptr)))
        {
            diagnostic = "Visual Studio opened the script project item but could not activate its document.";
            return false;
        }
        diagnostic.clear();
        return true;
    }
#endif

    std::filesystem::path ResolveManagedSolutionForExternalEditor(const std::filesystem::path& path,
                                                                  const std::filesystem::path& workingDirectory)
    {
        auto extension = path.extension().string();
        for (char& character : extension)
        {
            character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        }
        if (extension != ".cs" && extension != ".keireasm")
        {
            return {};
        }

        auto solutionName = workingDirectory.filename().string();
        for (char& character : solutionName)
        {
            if (!std::isalnum(static_cast<unsigned char>(character)) && character != '_' && character != '-')
            {
                character = '_';
            }
        }
        const auto candidate = workingDirectory / (solutionName + ".sln");
        return std::filesystem::is_regular_file(candidate) ? candidate : std::filesystem::path{};
    }

    std::vector<std::string> ResolveVisualStudioExternalEditorArguments(const std::filesystem::path& source,
                                                                        const std::filesystem::path& managedSolution)
    {
        if (source.empty())
        {
            throw std::invalid_argument("Visual Studio external-editor targeting requires a source file.");
        }
        const auto sourceText = PathToUtf8(source);
        if (managedSolution.empty())
        {
            return {sourceText};
        }
        return {PathToUtf8(managedSolution)};
    }

    std::vector<std::string> ResolveExternalEditorSourceArguments(const std::filesystem::path& source,
                                                                  const std::string_view editorName,
                                                                  const std::uint32_t line, const std::uint32_t column)
    {
        const auto sourceText = PathToUtf8(source);
        if (line != 0 && (editorName == "code" || editorName == "code-insiders" || editorName == "cursor" ||
                          editorName == "codium" || editorName == "vscodium"))
        {
            return {"--goto", sourceText + ":" + std::to_string(line) + ":" + std::to_string(std::max(column, 1U))};
        }
        return {sourceText};
    }

    bool OpenInExternalEditor(const std::filesystem::path& path, const std::filesystem::path& preferredEditor,
                              const std::filesystem::path& workingDirectory, std::string& diagnostic,
                              [[maybe_unused]] const bool reuseManagedSession, const std::uint32_t line,
                              const std::uint32_t column) noexcept
    {
        try
        {
            const auto source = std::filesystem::weakly_canonical(path);
            const auto working = std::filesystem::weakly_canonical(workingDirectory);
            if (!std::filesystem::is_regular_file(source) || !std::filesystem::is_directory(working))
            {
                diagnostic = "Source file or working directory does not exist.";
                return false;
            }
            auto editorName = preferredEditor.stem().string();
            for (char& character : editorName)
            {
                character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            }
#if defined(_WIN32)
            const auto primarySolution = ResolveManagedSolutionForExternalEditor(source, working);
            if (!primarySolution.empty() && (preferredEditor.empty() || editorName == "devenv"))
            {
                auto executable = preferredEditor;
                if (executable.empty())
                {
                    executable =
                        ResolveCompatibleVisualStudioExecutable(ManagedWorkspaceCompatibilityVisualStudioMajor);
                    if (!IsVisualStudioExecutableCompatible(executable, ManagedWorkspaceCompatibilityVisualStudioMajor))
                    {
                        executable = ResolveLegacyVisualStudioExecutable();
                    }
                    if (!IsVisualStudioExecutableCompatible(executable, ManagedWorkspaceCompatibilityVisualStudioMajor))
                    {
                        executable = ResolveAssociatedExecutable(primarySolution, working);
                    }
                }
                if (!IsVisualStudioExecutableCompatible(executable, ManagedWorkspaceCompatibilityVisualStudioMajor))
                {
                    diagnostic = "Select an installed Visual Studio 2022 (17.8+) or newer, or configure another "
                                 "supported external editor. The script was not opened as a loose file.";
                    return false;
                }
                const bool compatibilityIde =
                    !IsVisualStudioExecutableCompatible(executable, ManagedWorkspaceMinimumVisualStudioMajor);
                const auto solution = SelectManagedVisualStudioSolution(
                    primarySolution, compatibilityIde ? ManagedWorkspaceCompatibilityVisualStudioMajor
                                                      : ManagedWorkspaceMinimumVisualStudioMajor);
                const auto minimumMajor = ReadVisualStudioSolutionMinimumMajor(solution);
                if (!IsVisualStudioExecutableCompatible(executable, minimumMajor))
                {
                    diagnostic = compatibilityIde
                                     ? "The Visual Studio 2022 compatibility workspace is unavailable. Regenerate "
                                       "project files or repair this Editor installation's design-time APIs."
                                     : "The generated workspace requires a newer Visual Studio installation.";
                    return false;
                }

                bool matchingInstance = false;
                const auto wait = reuseManagedSession ? std::chrono::milliseconds(1500) : std::chrono::milliseconds(0);
                if (WaitForMatchingVisualStudioInstance(source, solution, executable, wait, minimumMajor,
                                                        matchingInstance, diagnostic, line, column))
                {
                    return true;
                }
                if (!matchingInstance)
                {
                    const auto arguments = ResolveVisualStudioExternalEditorArguments(source, solution);
                    if (!LaunchDetachedProcess(std::filesystem::weakly_canonical(executable), arguments, working,
                                               diagnostic))
                    {
                        return false;
                    }
                }
                OpenInVisualStudioWhenSolutionReady(source, solution, executable, minimumMajor, line, column);
                return true;
            }
#endif
            if (!preferredEditor.empty())
            {
                const auto arguments = ResolveExternalEditorSourceArguments(source, editorName, line, column);
                return LaunchDetachedProcess(std::filesystem::weakly_canonical(preferredEditor), arguments, working,
                                             diagnostic);
            }
#if defined(_WIN32)
            const auto result = reinterpret_cast<std::intptr_t>(ShellExecuteW(
                nullptr, L"open", source.wstring().c_str(), nullptr, working.wstring().c_str(), SW_SHOWNORMAL));
            if (result <= 32)
            {
                diagnostic = "Windows could not open the source with its associated application.";
                return false;
            }
            return true;
#else
#if defined(__APPLE__)
            const std::filesystem::path executable = "/usr/bin/open";
#else
            const std::filesystem::path executable = "/usr/bin/xdg-open";
#endif
            const std::vector<std::string> arguments{PathToUtf8(source)};
            return LaunchDetachedProcess(executable, arguments, working, diagnostic);
#endif
        }
        catch (const std::exception& error)
        {
            diagnostic = error.what();
            return false;
        }
    }

} // namespace Keire::Detail
