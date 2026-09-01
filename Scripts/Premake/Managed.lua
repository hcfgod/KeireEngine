local repositoryRoot = path.getabsolute("../..")
local managedProject = path.join(repositoryRoot, "KeireManaged/Keire.Managed.csproj")
local managedSourceRoot = path.join(repositoryRoot, "KeireManaged")
local managedOutput = path.join(repositoryRoot, "Build/Managed/Keire.Managed.dll")
local managedEditorProject = path.join(repositoryRoot, "KeireEditorManaged/Keire.Editor.Managed.csproj")
local managedEditorSourceRoot = path.join(repositoryRoot, "KeireEditorManaged")
local managedEditorOutput = path.join(repositoryRoot, "Build/Managed/Keire.Editor.Managed.dll")
local managedGeneratorSourceRoot = path.join(repositoryRoot, "KeireManaged.Generators")
local managedGeneratorOutput = path.join(repositoryRoot, "Build/Managed/Keire.Managed.Generators.dll")
local windowsManagedScript = path.join(repositoryRoot, "Scripts/Windows/build-managed.ps1")
local unixManagedScript = path.join(repositoryRoot, "Scripts/Unix/build-managed.sh")
local managedBuildInputs = {}
local managedBuildInputSet = {}
local managedScriptPrefix = _ACTION == "ninja" and "" or "../../../"

local function addManagedBuildInput(input)
    local normalized = path.translate(input, "/")
    local normalizedLower = normalized:lower()
    if normalizedLower:find("/bin/", 1, true) or normalizedLower:find("/obj/", 1, true) or
        normalizedLower:find("/build/", 1, true) or normalizedLower:sub(-4) == "/bin" or
        normalizedLower:sub(-4) == "/obj" or normalizedLower:sub(-6) == "/build" then
        return
    end
    if managedBuildInputSet[normalizedLower] then
        return
    end
    managedBuildInputSet[normalizedLower] = true
    table.insert(managedBuildInputs, input)
end

-- Ninja needs directory timestamps to notice source additions and removals without an intervening generation step.
-- Visual Studio rejects directories as custom-build file dependencies; the repository launcher regenerates its project
-- inventory before every normal build. The proxy uses a distinct name so Ninja never confuses the source directory
-- with the project phony target.
local managedSourceRoots = { managedSourceRoot, managedEditorSourceRoot, managedGeneratorSourceRoot }
if _ACTION == "ninja" then
    for _, sourceRoot in ipairs(managedSourceRoots) do
        addManagedBuildInput(sourceRoot)
        for _, directory in ipairs(os.matchdirs(path.join(sourceRoot, "**"))) do
            addManagedBuildInput(directory)
        end
    end
end
for _, sourceRoot in ipairs(managedSourceRoots) do
    for _, source in ipairs(os.matchfiles(path.join(sourceRoot, "**.cs"))) do
        addManagedBuildInput(source)
    end
end
table.sort(managedBuildInputs)

KeireManagedProject = ProjectConfig.PROJECT_NAMESPACE .. "ManagedRuntimeApi"

function AddKeireManagedRuntimeDependency()
    if _ACTION == "ninja" then
        links { KeireManagedProject }
    else
        dependson { KeireManagedProject }
    end
end

function AddKeireManagedHostStaging(includeEditorApi)
    if _ACTION == "ninja" then
        return
    end

    local commandRepositoryRoot = _ACTION == "gmake" and "." or ".."
    local editorApiSwitch = includeEditorApi and " -IncludeEditorApi" or ""
    local editorApiArgument = includeEditorApi and " true" or " false"

    filter "system:windows"
        postbuildcommands
        {
            'powershell -NoProfile -ExecutionPolicy Bypass -File "' .. commandRepositoryRoot ..
                '/Scripts/Windows/stage-managed-host.ps1" -Root "' .. commandRepositoryRoot ..
                '" -Configuration "%{cfg.buildcfg}" -Architecture "%{cfg.architecture}" -Target "%{prj.name}"' ..
                editorApiSwitch
        }

    filter { "system:linux or macosx" }
        postbuildcommands
        {
            'bash "' .. commandRepositoryRoot .. '/Scripts/Unix/stage-managed-host.sh" "' ..
                commandRepositoryRoot .. '" "%{cfg.buildcfg}" "%{cfg.system}" "%{cfg.architecture}" "%{prj.name}"' ..
                editorApiArgument
        }

    filter {}
end

project(KeireManagedProject)
    location "../../Build/Projects/KeireManaged"
    objdir ("../../Build/Intermediates/" .. IntermediateOutputDir .. "/" .. KeireManagedProject)

    if _ACTION == "ninja" then
        kind "StaticLib"
        targetname(ProjectConfig.PROJECT_NAMESPACE .. "ManagedBuildProxy")
        ApplyCommonProjectSettings("../..")
    else
        kind "Utility"
    end

    files
    {
        "ManagedBuildAnchor.cpp",
        "../../KeireManaged/**.cs",
        "../../KeireManaged/**.csproj",
        "../../KeireEditorManaged/**.cs",
        "../../KeireEditorManaged/**.csproj",
        "../../KeireManaged.Generators/**.cs",
        "../../KeireManaged.Generators/**.csproj"
    }

    removefiles
    {
        "../../KeireManaged/bin/**",
        "../../KeireManaged/obj/**",
        "../../KeireEditorManaged/bin/**",
        "../../KeireEditorManaged/obj/**",
        "../../KeireManaged.Generators/bin/**",
        "../../KeireManaged.Generators/obj/**"
    }

    filter { "files:**.csproj", "system:windows" }
        buildcommands
        {
            'powershell -NoProfile -ExecutionPolicy Bypass -File ' ..
                managedScriptPrefix .. "Scripts/Windows/build-managed.ps1"
        }
        buildinputs { windowsManagedScript }

    filter { "files:**.csproj", "system:linux or macosx" }
        buildcommands
        {
            "bash " .. managedScriptPrefix .. "Scripts/Unix/build-managed.sh"
        }
        buildinputs { unixManagedScript }

    filter "files:**.csproj"
        buildmessage "Building managed runtime API"
        buildinputs(managedBuildInputs)
        buildoutputs { managedOutput, managedEditorOutput, managedGeneratorOutput }
        linkbuildoutputs "Off"

    filter {}
