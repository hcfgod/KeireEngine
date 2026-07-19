local function ApplyHeaderOnlyKind()
    if _ACTION == "ninja" or _ACTION == "gmake" or _ACTION == "gmake2" then
        kind "None"
    else
        kind "Utility"
    end
end

project(EnTTProject)
    location "../../Build/Projects/EnTT"
    ApplyHeaderOnlyKind()
    warnings "Off"
    files
    {
        "../../Vendor/entt/src/entt/**.h",
        "../../Vendor/entt/src/entt/**.hpp"
    }

project(GLMProject)
    location "../../Build/Projects/GLM"
    ApplyHeaderOnlyKind()
    warnings "Off"
    files
    {
        "../../Vendor/glm/glm/**.h",
        "../../Vendor/glm/glm/**.hpp",
        "../../Vendor/glm/glm/**.inl"
    }
