function KeireDefineSourceModulePack(name, root, coreTarget)
    project(name)
        kind "StaticLib"
        language "C++"
        cppdialect "C++20"
        staticruntime "off"
        files
        {
            path.join(root, "Include/**.h"),
            path.join(root, "Include/**.hpp"),
            path.join(root, "Source/**.cpp")
        }
        includedirs
        {
            path.join(root, "Include")
        }
        links
        {
            coreTarget
        }
end

function KeireLinkSourceModulePack(name, includeDirectory)
    includedirs
    {
        includeDirectory
    }
    links
    {
        name
    }
end
