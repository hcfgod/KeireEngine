#pragma once
#include "Keire/Scripting/ManagedAssemblyAsset.h"
#include <map>

namespace Keire::Detail
{
    void ValidateManagedAssemblyPolicies(const ManagedAssemblyDefinition& definition);
    void ApplyManagedAssemblyPolicies(const std::filesystem::path& projectRoot,
                                      std::vector<ManagedAssemblyGraphEntry>& assemblies,
                                      const ManagedAssemblyBuildContext& context);
    void AddManagedAssemblyReferenceRoots(const std::filesystem::path& projectRoot,
                                          const std::vector<ManagedAssemblyGraphEntry>& assemblies,
                                          std::map<std::filesystem::path, std::size_t>& owners);
} // namespace Keire::Detail
