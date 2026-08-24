local repositoryRoot = path.getabsolute("../..")
local managedProject = path.join(repositoryRoot, "KeireManaged/Keire.Managed.csproj")
local managedSourceRoot = path.join(repositoryRoot, "KeireManaged")
local managedOutput = path.join(repositoryRoot, "Build/Managed/Keire.Managed.dll")
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
if _ACTION == "ninja" then
    addManagedBuildInput(managedSourceRoot)
    for _, directory in ipairs(os.matchdirs(path.join(managedSourceRoot, "**"))) do
        addManagedBuildInput(directory)
    end
end
for _, source in ipairs(os.matchfiles(path.join(managedSourceRoot, "**.cs"))) do
    addManagedBuildInput(source)
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

function AddKeireManagedHostStaging()
    if _ACTION == "ninja" then
        return
    end

    local commandRepositoryRoot = _ACTION == "gmake" and "." or ".."

    filter "system:windows"
        postbuildcommands
        {
            'powershell -NoProfile -ExecutionPolicy Bypass -File "' .. commandRepositoryRoot ..
                '/Scripts/Windows/stage-managed-host.ps1" -Root "' .. commandRepositoryRoot ..
                '" -Configuration "%{cfg.buildcfg}" -Architecture "%{cfg.architecture}" -Target "%{prj.name}"'
        }

    filter { "system:linux or macosx" }
        postbuildcommands
        {
            'bash "' .. commandRepositoryRoot .. '/Scripts/Unix/stage-managed-host.sh" "' ..
                commandRepositoryRoot .. '" "%{cfg.buildcfg}" "%{cfg.system}" "%{cfg.architecture}" "%{prj.name}"'
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
        "../../KeireManaged/**.csproj"
    }

    removefiles
    {
        "../../KeireManaged/bin/**",
        "../../KeireManaged/obj/**"
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
        buildoutputs { managedOutput }
        linkbuildoutputs "Off"

    filter {}
