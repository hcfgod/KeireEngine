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

-- Directory timestamps make raw generated-project builds notice source additions and removals. The proxy project uses
-- a distinct name so Ninja never confuses the KeireManaged source directory with the project phony target.
addManagedBuildInput(managedSourceRoot)
for _, directory in ipairs(os.matchdirs(path.join(managedSourceRoot, "**"))) do
    addManagedBuildInput(directory)
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

project(KeireManagedProject)
    location "../../Build/Projects/KeireManaged"

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
