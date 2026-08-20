#include "KeireInternal/Scripting/ManagedRuntimeRendering.h"

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4146)
#endif
#include <Coral/Assembly.hpp>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <algorithm>
#include <limits>
#include <span>
#include <string>

namespace Keire::Detail
{
    namespace
    {
        thread_local IScriptRuntimeServices* ActiveServices = nullptr;

        [[nodiscard]] bool ValidComponent(const std::uint8_t component) noexcept
        {
            return component <= static_cast<std::uint8_t>(ManagedRenderingComponent::SpotLight);
        }

        [[nodiscard]] std::uint8_t GetScalar(const std::uint64_t high, const std::uint64_t low,
                                             const std::uint8_t component, const std::uint8_t property,
                                             float* value) noexcept
        {
            if (!ActiveServices || !value || !ValidComponent(component) ||
                property > static_cast<std::uint8_t>(ManagedRenderingScalarProperty::IndirectMultiplier))
            {
                return 0;
            }
            const auto result = ActiveServices->ReadManagedRenderingScalar(
                AssetId(high, low), static_cast<ManagedRenderingComponent>(component),
                static_cast<ManagedRenderingScalarProperty>(property));
            if (!result)
                return 0;
            *value = *result;
            return 1;
        }

        [[nodiscard]] std::uint8_t SetScalar(const std::uint64_t high, const std::uint64_t low,
                                             const std::uint8_t component, const std::uint8_t property,
                                             const float value) noexcept
        {
            return ActiveServices && ValidComponent(component) &&
                           property <= static_cast<std::uint8_t>(ManagedRenderingScalarProperty::IndirectMultiplier) &&
                           ActiveServices->SetManagedRenderingScalar(
                               AssetId(high, low), static_cast<ManagedRenderingComponent>(component),
                               static_cast<ManagedRenderingScalarProperty>(property), value)
                       ? 1
                       : 0;
        }

        [[nodiscard]] std::uint8_t GetInteger(const std::uint64_t high, const std::uint64_t low,
                                              const std::uint8_t component, const std::uint8_t property,
                                              std::int32_t* value) noexcept
        {
            if (!ActiveServices || !value || !ValidComponent(component) ||
                property > static_cast<std::uint8_t>(ManagedRenderingIntegerProperty::ShadowResolution))
            {
                return 0;
            }
            const auto result = ActiveServices->ReadManagedRenderingInteger(
                AssetId(high, low), static_cast<ManagedRenderingComponent>(component),
                static_cast<ManagedRenderingIntegerProperty>(property));
            if (!result)
                return 0;
            *value = *result;
            return 1;
        }

        [[nodiscard]] std::uint8_t SetInteger(const std::uint64_t high, const std::uint64_t low,
                                              const std::uint8_t component, const std::uint8_t property,
                                              const std::int32_t value) noexcept
        {
            return ActiveServices && ValidComponent(component) &&
                           property <= static_cast<std::uint8_t>(ManagedRenderingIntegerProperty::ShadowResolution) &&
                           ActiveServices->SetManagedRenderingInteger(
                               AssetId(high, low), static_cast<ManagedRenderingComponent>(component),
                               static_cast<ManagedRenderingIntegerProperty>(property), value)
                       ? 1
                       : 0;
        }

        [[nodiscard]] std::uint8_t GetFlag(const std::uint64_t high, const std::uint64_t low,
                                           const std::uint8_t component, const std::uint8_t property,
                                           std::uint8_t* value) noexcept
        {
            if (!ActiveServices || !value || !ValidComponent(component) ||
                property > static_cast<std::uint8_t>(ManagedRenderingFlagProperty::ContactShadows))
            {
                return 0;
            }
            const auto result = ActiveServices->ReadManagedRenderingFlag(
                AssetId(high, low), static_cast<ManagedRenderingComponent>(component),
                static_cast<ManagedRenderingFlagProperty>(property));
            if (!result)
                return 0;
            *value = *result ? 1 : 0;
            return 1;
        }

        [[nodiscard]] std::uint8_t SetFlag(const std::uint64_t high, const std::uint64_t low,
                                           const std::uint8_t component, const std::uint8_t property,
                                           const std::uint8_t value) noexcept
        {
            return ActiveServices && ValidComponent(component) &&
                           property <= static_cast<std::uint8_t>(ManagedRenderingFlagProperty::ContactShadows) &&
                           ActiveServices->SetManagedRenderingFlag(
                               AssetId(high, low), static_cast<ManagedRenderingComponent>(component),
                               static_cast<ManagedRenderingFlagProperty>(property), value != 0)
                       ? 1
                       : 0;
        }

        [[nodiscard]] std::uint8_t GetVector(const std::uint64_t high, const std::uint64_t low,
                                             const std::uint8_t component, const std::uint8_t property,
                                             Vector2* value) noexcept
        {
            if (!ActiveServices || !value || !ValidComponent(component) ||
                property > static_cast<std::uint8_t>(ManagedRenderingVectorProperty::CookieOffset))
            {
                return 0;
            }
            const auto result = ActiveServices->ReadManagedRenderingVector(
                AssetId(high, low), static_cast<ManagedRenderingComponent>(component),
                static_cast<ManagedRenderingVectorProperty>(property));
            if (!result)
                return 0;
            *value = *result;
            return 1;
        }

        [[nodiscard]] std::uint8_t SetVector(const std::uint64_t high, const std::uint64_t low,
                                             const std::uint8_t component, const std::uint8_t property,
                                             const Vector2 value) noexcept
        {
            return ActiveServices && ValidComponent(component) &&
                           property <= static_cast<std::uint8_t>(ManagedRenderingVectorProperty::CookieOffset) &&
                           ActiveServices->SetManagedRenderingVector(
                               AssetId(high, low), static_cast<ManagedRenderingComponent>(component),
                               static_cast<ManagedRenderingVectorProperty>(property), value)
                       ? 1
                       : 0;
        }

        [[nodiscard]] std::uint8_t GetColor(const std::uint64_t high, const std::uint64_t low,
                                            const std::uint8_t component, const std::uint8_t property,
                                            Color* value) noexcept
        {
            if (!ActiveServices || !value || !ValidComponent(component) ||
                property > static_cast<std::uint8_t>(ManagedRenderingColorProperty::LightColor))
            {
                return 0;
            }
            const auto result = ActiveServices->ReadManagedRenderingColor(
                AssetId(high, low), static_cast<ManagedRenderingComponent>(component),
                static_cast<ManagedRenderingColorProperty>(property));
            if (!result)
                return 0;
            *value = *result;
            return 1;
        }

        [[nodiscard]] std::uint8_t SetColor(const std::uint64_t high, const std::uint64_t low,
                                            const std::uint8_t component, const std::uint8_t property,
                                            const Color value) noexcept
        {
            return ActiveServices && ValidComponent(component) &&
                           property <= static_cast<std::uint8_t>(ManagedRenderingColorProperty::LightColor) &&
                           ActiveServices->SetManagedRenderingColor(
                               AssetId(high, low), static_cast<ManagedRenderingComponent>(component),
                               static_cast<ManagedRenderingColorProperty>(property), value)
                       ? 1
                       : 0;
        }

        [[nodiscard]] std::uint8_t GetAsset(const std::uint64_t high, const std::uint64_t low,
                                            const std::uint8_t component, const std::uint8_t property,
                                            AssetId* value) noexcept
        {
            if (!ActiveServices || !value || !ValidComponent(component) ||
                property > static_cast<std::uint8_t>(ManagedRenderingAssetProperty::Cookie))
            {
                return 0;
            }
            const auto result = ActiveServices->ReadManagedRenderingAsset(
                AssetId(high, low), static_cast<ManagedRenderingComponent>(component),
                static_cast<ManagedRenderingAssetProperty>(property));
            if (!result)
                return 0;
            *value = *result;
            return 1;
        }

        [[nodiscard]] std::uint8_t SetAsset(const std::uint64_t high, const std::uint64_t low,
                                            const std::uint8_t component, const std::uint8_t property,
                                            const AssetId value) noexcept
        {
            return ActiveServices && ValidComponent(component) &&
                           property <= static_cast<std::uint8_t>(ManagedRenderingAssetProperty::Cookie) &&
                           ActiveServices->SetManagedRenderingAsset(
                               AssetId(high, low), static_cast<ManagedRenderingComponent>(component),
                               static_cast<ManagedRenderingAssetProperty>(property), value)
                       ? 1
                       : 0;
        }

        [[nodiscard]] int GetMaterials(const std::uint64_t high, const std::uint64_t low, AssetId* destination,
                                       const int capacity) noexcept
        {
            if (!ActiveServices || capacity < 0)
                return -1;
            const auto materials = ActiveServices->ReadManagedRendererMaterials(AssetId(high, low));
            if (!materials || materials->size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
                return -1;
            const auto count = static_cast<int>(materials->size());
            if (!destination || capacity == 0)
                return count;
            if (capacity < count)
                return -1;
            std::ranges::copy(*materials, destination);
            return count;
        }

        [[nodiscard]] std::uint8_t SetMaterials(const std::uint64_t high, const std::uint64_t low,
                                                const AssetId* materials, const int count) noexcept
        {
            if (!ActiveServices || count < 0 || count > 256 || (count > 0 && !materials))
                return 0;
            return ActiveServices->SetManagedRendererMaterials(AssetId(high, low),
                                                               std::span(materials, static_cast<std::size_t>(count)))
                       ? 1
                       : 0;
        }

        template <typename T>
        [[nodiscard]] std::uint8_t SetMaterialProperty(const std::uint64_t high, const std::uint64_t low,
                                                       const Coral::String name, const T value) noexcept
        {
            return ActiveServices && ActiveServices->SetManagedMaterialProperty(AssetId(high, low),
                                                                                static_cast<std::string>(name), value)
                       ? 1
                       : 0;
        }

        [[nodiscard]] std::uint8_t ResetMaterialProperty(const std::uint64_t high, const std::uint64_t low,
                                                         const Coral::String name) noexcept
        {
            return ActiveServices && ActiveServices->ResetManagedMaterialProperty(AssetId(high, low),
                                                                                  static_cast<std::string>(name))
                       ? 1
                       : 0;
        }

        [[nodiscard]] std::uint8_t ClearMaterialProperties(const std::uint64_t high, const std::uint64_t low) noexcept
        {
            return ActiveServices && ActiveServices->ClearManagedMaterialProperties(AssetId(high, low)) ? 1 : 0;
        }
    } // namespace

    ManagedRuntimeRenderingScope::ManagedRuntimeRenderingScope(IScriptRuntimeServices* services) noexcept
        : m_Previous(ActiveServices)
    {
        ActiveServices = services;
    }

    ManagedRuntimeRenderingScope::~ManagedRuntimeRenderingScope() { ActiveServices = m_Previous; }

    void RegisterManagedRuntimeRendering(Coral::ManagedAssembly& assembly)
    {
        assembly.AddInternalCall("Keire.NativeRuntimeRendering", "GetScalarIcall", reinterpret_cast<void*>(&GetScalar));
        assembly.AddInternalCall("Keire.NativeRuntimeRendering", "SetScalarIcall", reinterpret_cast<void*>(&SetScalar));
        assembly.AddInternalCall("Keire.NativeRuntimeRendering", "GetIntegerIcall",
                                 reinterpret_cast<void*>(&GetInteger));
        assembly.AddInternalCall("Keire.NativeRuntimeRendering", "SetIntegerIcall",
                                 reinterpret_cast<void*>(&SetInteger));
        assembly.AddInternalCall("Keire.NativeRuntimeRendering", "GetFlagIcall", reinterpret_cast<void*>(&GetFlag));
        assembly.AddInternalCall("Keire.NativeRuntimeRendering", "SetFlagIcall", reinterpret_cast<void*>(&SetFlag));
        assembly.AddInternalCall("Keire.NativeRuntimeRendering", "GetVectorIcall", reinterpret_cast<void*>(&GetVector));
        assembly.AddInternalCall("Keire.NativeRuntimeRendering", "SetVectorIcall", reinterpret_cast<void*>(&SetVector));
        assembly.AddInternalCall("Keire.NativeRuntimeRendering", "GetColorIcall", reinterpret_cast<void*>(&GetColor));
        assembly.AddInternalCall("Keire.NativeRuntimeRendering", "SetColorIcall", reinterpret_cast<void*>(&SetColor));
        assembly.AddInternalCall("Keire.NativeRuntimeRendering", "GetAssetIcall", reinterpret_cast<void*>(&GetAsset));
        assembly.AddInternalCall("Keire.NativeRuntimeRendering", "SetAssetIcall", reinterpret_cast<void*>(&SetAsset));
        assembly.AddInternalCall("Keire.NativeRuntimeRendering", "GetMaterialsIcall",
                                 reinterpret_cast<void*>(&GetMaterials));
        assembly.AddInternalCall("Keire.NativeRuntimeRendering", "SetMaterialsIcall",
                                 reinterpret_cast<void*>(&SetMaterials));
        assembly.AddInternalCall("Keire.NativeRuntimeRendering", "SetMaterialFloatIcall",
                                 reinterpret_cast<void*>(&SetMaterialProperty<float>));
        assembly.AddInternalCall("Keire.NativeRuntimeRendering", "SetMaterialVector2Icall",
                                 reinterpret_cast<void*>(&SetMaterialProperty<Vector2>));
        assembly.AddInternalCall("Keire.NativeRuntimeRendering", "SetMaterialVector3Icall",
                                 reinterpret_cast<void*>(&SetMaterialProperty<Vector3>));
        assembly.AddInternalCall("Keire.NativeRuntimeRendering", "SetMaterialVector4Icall",
                                 reinterpret_cast<void*>(&SetMaterialProperty<Vector4>));
        assembly.AddInternalCall("Keire.NativeRuntimeRendering", "SetMaterialColorIcall",
                                 reinterpret_cast<void*>(&SetMaterialProperty<Color>));
        assembly.AddInternalCall("Keire.NativeRuntimeRendering", "SetMaterialTextureIcall",
                                 reinterpret_cast<void*>(&SetMaterialProperty<AssetId>));
        assembly.AddInternalCall("Keire.NativeRuntimeRendering", "ResetMaterialPropertyIcall",
                                 reinterpret_cast<void*>(&ResetMaterialProperty));
        assembly.AddInternalCall("Keire.NativeRuntimeRendering", "ClearMaterialPropertiesIcall",
                                 reinterpret_cast<void*>(&ClearMaterialProperties));
    }
} // namespace Keire::Detail
