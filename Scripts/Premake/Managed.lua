local repositoryRoot = path.getabsolute("../..")
local managedProject = path.join(repositoryRoot, "KeireManaged/Keire.Managed.csproj")
local managedOutput = path.join(repositoryRoot, "Build/Managed")
local managedIntermediate = path.join(repositoryRoot, "Build/Intermediates/Managed/")

KeireManagedProject = ProjectConfig.PROJECT_NAMESPACE .. "Managed"

project(KeireManagedProject)
    location "../../Build/Projects/KeireManaged"
    kind "Utility"

    files
    {
        "../../KeireManaged/**.cs",
        "../../KeireManaged/**.csproj"
    }

    filter "system:windows"
        buildcommands
        {
            '"' .. path.join(repositoryRoot, "Build/Dependencies/dotnet-sdk/dotnet.exe") .. '" build "' ..
                managedProject .. '" --nologo --configuration Release --output "' .. managedOutput ..
                '" --property:BaseIntermediateOutputPath="' .. managedIntermediate .. '"'
        }

    filter { "system:linux or macosx" }
        buildcommands
        {
            '"' .. path.join(repositoryRoot, "Build/Dependencies/dotnet-sdk/dotnet") .. '" build "' ..
                managedProject .. '" --nologo --configuration Release --output "' .. managedOutput ..
                '" --property:BaseIntermediateOutputPath="' .. managedIntermediate .. '"'
        }

    filter {}

    buildinputs
    {
        "../../KeireManaged/**.cs",
        "../../KeireManaged/**.csproj"
    }

    buildoutputs
    {
        path.join(managedOutput, "Keire.Managed.dll")
    }
