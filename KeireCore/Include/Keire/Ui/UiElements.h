#pragma once

#include "Keire/Api.h"
#include "Keire/Assets/Asset.h"
#include "Keire/Math/Math.h"
#include "Keire/Ref.h"
#include "Keire/Ui/UiToolkit.h"

#include <algorithm>
#include <any>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeindex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Keire::Ui
{
    using VisualTreeAsset = UiVisualTreeAsset;
    using StyleSheet = UiStyleSheetAsset;
    using PanelSettings = UiPanelSettingsAsset;

    enum class TrickleDown : std::uint8_t
    {
        No,
        Yes
    };

    enum class PropagationPhase : std::uint8_t
    {
        None,
        TrickleDown,
        AtTarget,
        BubbleUp
    };

    enum class BindingMode : std::uint8_t
    {
        OneWay,
        TwoWay,
        OneTime
    };

    enum class NavigationDirection : std::uint8_t
    {
        Previous,
        Next,
        Left,
        Right,
        Up,
        Down
    };

    class VisualElement;

    class KEIRE_API EventBase
    {
      public:
        virtual ~EventBase() = default;

        [[nodiscard]] VisualElement* Target() const noexcept { return m_Target; }
        [[nodiscard]] VisualElement* CurrentTarget() const noexcept { return m_CurrentTarget; }
        [[nodiscard]] PropagationPhase Phase() const noexcept { return m_Phase; }
        [[nodiscard]] bool PropagationStopped() const noexcept { return m_PropagationStopped; }
        [[nodiscard]] bool ImmediatePropagationStopped() const noexcept { return m_ImmediatePropagationStopped; }
        [[nodiscard]] bool DefaultPrevented() const noexcept { return m_DefaultPrevented; }
        [[nodiscard]] virtual bool Bubbles() const noexcept { return true; }
        [[nodiscard]] virtual bool TricklesDown() const noexcept { return true; }

        void StopPropagation() noexcept { m_PropagationStopped = true; }
        void StopImmediatePropagation() noexcept
        {
            m_ImmediatePropagationStopped = true;
            m_PropagationStopped = true;
        }
        void PreventDefault() noexcept { m_DefaultPrevented = true; }

      private:
        friend class VisualElement;
        VisualElement* m_Target = nullptr;
        VisualElement* m_CurrentTarget = nullptr;
        PropagationPhase m_Phase = PropagationPhase::None;
        bool m_PropagationStopped = false;
        bool m_ImmediatePropagationStopped = false;
        bool m_DefaultPrevented = false;
    };

    class KEIRE_API PointerEventBase : public EventBase
    {
      public:
        std::int32_t PointerId = 0;
        std::int32_t Button = 0;
        Vector2 Position;
    };

    class KEIRE_API PointerDownEvent final : public PointerEventBase
    {
    };
    class KEIRE_API PointerUpEvent final : public PointerEventBase
    {
    };
    class KEIRE_API PointerMoveEvent final : public PointerEventBase
    {
    };
    class KEIRE_API PointerEnterEvent final : public PointerEventBase
    {
    };
    class KEIRE_API PointerLeaveEvent final : public PointerEventBase
    {
    };
    class KEIRE_API ClickEvent final : public PointerEventBase
    {
    };
    class KEIRE_API FocusInEvent final : public EventBase
    {
    };
    class KEIRE_API FocusOutEvent final : public EventBase
    {
    };
    class KEIRE_API SubmitEvent final : public EventBase
    {
    };
    class KEIRE_API CancelEvent final : public EventBase
    {
    };

    class KEIRE_API KeyDownEvent final : public EventBase
    {
      public:
        std::string Key;
        bool Shift = false;
        bool Control = false;
        bool Alt = false;
    };

    class KEIRE_API TextInputEvent final : public EventBase
    {
      public:
        std::string Text;
    };

    class KEIRE_API NavigationMoveEvent final : public EventBase
    {
      public:
        NavigationDirection Direction = NavigationDirection::Next;
    };

    template <typename T> class ChangeEvent final : public EventBase
    {
      public:
        T PreviousValue{};
        T NewValue{};
    };

    struct DataBinding
    {
        std::string SourcePath;
        BindingMode Mode = BindingMode::OneWay;
        std::function<std::any()> Read;
        std::function<void(const std::any&)> Write;
        std::function<bool(VisualElement&, std::string_view, const std::any&, std::string&)> Apply;
        std::function<std::any(const VisualElement&, std::string_view)> Capture;
        std::function<void(VisualElement&, std::string_view, const std::any&)> Restore;
    };

    struct BindingDiagnostic
    {
        std::string TargetProperty;
        std::string SourcePath;
        std::string Message;
    };

    struct CallbackToken
    {
        std::uint64_t Value = 0;

        [[nodiscard]] explicit operator bool() const noexcept { return Value != 0; }
        [[nodiscard]] bool operator==(const CallbackToken&) const = default;
    };

    template <typename T> class UQueryBuilder;

    class KEIRE_API VisualElement : public RefCounted
    {
      public:
        VisualElement();
        ~VisualElement() override;

        [[nodiscard]] AssetId StableId() const noexcept { return m_StableId; }
        void SetStableId(AssetId value);
        [[nodiscard]] std::string_view Name() const noexcept { return m_Name; }
        void SetName(std::string value);
        [[nodiscard]] VisualElement* Parent() const noexcept { return m_Parent; }
        [[nodiscard]] std::span<const Ref<VisualElement>> Children() const noexcept { return m_Children; }
        [[nodiscard]] const RuntimeUiStyle& Style() const noexcept { return m_Style; }
        [[nodiscard]] RuntimeUiStyle& Style() noexcept { return m_Style; }
        [[nodiscard]] bool EnabledSelf() const noexcept { return m_Enabled; }
        [[nodiscard]] bool EnabledInHierarchy() const noexcept;
        [[nodiscard]] bool Focusable() const noexcept { return m_Focusable; }
        void SetFocusable(bool value) noexcept;
        [[nodiscard]] std::int32_t TabIndex() const noexcept { return m_TabIndex; }
        void SetTabIndex(std::int32_t value) noexcept { m_TabIndex = value; }
        [[nodiscard]] std::string_view Tooltip() const noexcept { return m_Tooltip; }
        void SetTooltip(std::string value) { m_Tooltip = std::move(value); }
        [[nodiscard]] const std::any& UserData() const noexcept { return m_UserData; }
        void SetUserData(std::any value) { m_UserData = std::move(value); }
        [[nodiscard]] std::string_view Type() const noexcept { return TypeName(); }

        void Add(Ref<VisualElement> child);
        void Insert(std::size_t index, Ref<VisualElement> child);
        [[nodiscard]] bool Remove(const Ref<VisualElement>& child) noexcept;
        void RemoveFromHierarchy() noexcept;
        void Clear() noexcept;

        void AddToClassList(std::string className);
        [[nodiscard]] bool RemoveFromClassList(std::string_view className);
        [[nodiscard]] bool ClassListContains(std::string_view className) const;
        [[nodiscard]] std::vector<std::string> ClassList() const;
        void EnableInClassList(std::string className, bool enabled);
        void SetEnabled(bool enabled) noexcept;

        void Focus();
        void Blur();
        [[nodiscard]] bool HasFocus() const noexcept;
        void CapturePointer(std::int32_t pointerId);
        void ReleasePointer(std::int32_t pointerId) noexcept;
        [[nodiscard]] bool HasPointerCapture(std::int32_t pointerId) const noexcept;
        [[nodiscard]] VisualElement* CapturingElement(std::int32_t pointerId) const noexcept;

        template <typename TEvent>
        CallbackToken RegisterCallback(std::function<void(TEvent&)> callback, TrickleDown trickle = TrickleDown::No)
        {
            static_assert(std::derived_from<TEvent, EventBase>);
            if (!callback)
                throw std::invalid_argument("A UI event callback is required.");
            if (m_NextCallbackToken == (std::numeric_limits<std::uint64_t>::max)())
                throw std::overflow_error("The visual element callback-token sequence is exhausted.");
            const CallbackToken token{m_NextCallbackToken++};
            m_Callbacks[typeid(TEvent)].push_back({.Token = token,
                                                   .Callback = [callback = std::move(callback)](EventBase& event)
                                                   { callback(static_cast<TEvent&>(event)); },
                                                   .Trickle = trickle == TrickleDown::Yes});
            return token;
        }

        [[nodiscard]] bool UnregisterCallback(CallbackToken token) noexcept;
        void SendEvent(EventBase& event);

        template <typename T = VisualElement>
        [[nodiscard]] UQueryBuilder<T> Query(std::string name = {}, std::string className = {});
        template <typename T = VisualElement> [[nodiscard]] Ref<T> Q(std::string name = {}, std::string className = {});

        void SetBinding(std::string property, DataBinding binding);
        [[nodiscard]] bool RemoveBinding(std::string_view property) noexcept;
        void SetAuthoredBinding(UiBindingDefinition binding);
        [[nodiscard]] std::span<const UiBindingDefinition> AuthoredBindings() const noexcept
        {
            return m_AuthoredBindings;
        }
        void UpdateBindings();
        [[nodiscard]] std::optional<BindingDiagnostic> LastBindingDiagnostic() const;
        void WriteBinding(std::string_view property, const std::any& value);

      protected:
        [[nodiscard]] virtual std::string_view TypeName() const noexcept { return "VisualElement"; }

      private:
        struct CallbackEntry
        {
            CallbackToken Token;
            std::function<void(EventBase&)> Callback;
            bool Trickle = false;
        };

        struct BindingEntry
        {
            DataBinding Binding;
            bool Applied = false;
        };

        [[nodiscard]] VisualElement& Root() noexcept;
        [[nodiscard]] const VisualElement& Root() const noexcept;
        [[nodiscard]] bool Contains(const VisualElement* element) const noexcept;
        void Invoke(EventBase& event, bool trickle);
        void ReleaseOwnedState(const VisualElement* subtree) noexcept;
        void Collect(std::vector<Ref<VisualElement>>& output, const std::function<bool(const VisualElement&)>& filter);
        [[nodiscard]] std::any CaptureBoundProperty(std::string_view property) const;
        [[nodiscard]] bool ApplyBoundProperty(std::string_view property, const std::any& value, std::string& error);
        void RestoreBoundProperty(std::string_view property, const std::any& value);

        AssetId m_StableId;
        std::string m_Name;
        VisualElement* m_Parent = nullptr;
        std::vector<Ref<VisualElement>> m_Children;
        std::unordered_set<std::string> m_Classes;
        RuntimeUiStyle m_Style;
        std::unordered_map<std::type_index, std::vector<CallbackEntry>> m_Callbacks;
        std::unordered_map<std::string, BindingEntry> m_Bindings;
        std::vector<UiBindingDefinition> m_AuthoredBindings;
        std::optional<BindingDiagnostic> m_LastBindingDiagnostic;
        std::unordered_map<std::int32_t, VisualElement*> m_PointerCaptures;
        VisualElement* m_FocusedElement = nullptr;
        std::any m_UserData;
        std::string m_Tooltip;
        std::uint64_t m_NextCallbackToken = 1;
        std::int32_t m_TabIndex = 0;
        bool m_Enabled = true;
        bool m_Focusable = false;
        bool m_ApplyingBindings = false;

        template <typename T> friend class UQueryBuilder;
    };

    template <typename T> class UQueryBuilder
    {
      public:
        UQueryBuilder(VisualElement& root, std::string name, std::string className)
            : m_Root(root), m_Name(std::move(name)), m_ClassName(std::move(className))
        {
            static_assert(std::derived_from<T, VisualElement>);
        }

        [[nodiscard]] std::vector<Ref<T>> ToList()
        {
            std::vector<Ref<VisualElement>> matches;
            m_Root.Collect(matches,
                           [this](const VisualElement& element)
                           {
                               return dynamic_cast<const T*>(&element) &&
                                      (m_Name.empty() || element.Name() == m_Name) &&
                                      (m_ClassName.empty() || element.ClassListContains(m_ClassName));
                           });
            std::vector<Ref<T>> result;
            result.reserve(matches.size());
            for (const auto& match : matches)
            {
                if (auto typed = DynamicRefCast<T>(match))
                    result.push_back(std::move(typed));
            }
            return result;
        }

        [[nodiscard]] Ref<T> First()
        {
            auto matches = ToList();
            return matches.empty() ? Ref<T>{} : matches.front();
        }

      private:
        VisualElement& m_Root;
        std::string m_Name;
        std::string m_ClassName;
    };

    template <typename T> UQueryBuilder<T> VisualElement::Query(std::string name, std::string className)
    {
        return UQueryBuilder<T>(*this, std::move(name), std::move(className));
    }

    template <typename T> Ref<T> VisualElement::Q(std::string name, std::string className)
    {
        return Query<T>(std::move(name), std::move(className)).First();
    }

    class KEIRE_API TextElement : public VisualElement
    {
      public:
        [[nodiscard]] std::string_view Text() const noexcept { return m_Text; }
        void SetText(std::string value);

      protected:
        [[nodiscard]] std::string_view TypeName() const noexcept override { return "TextElement"; }

      private:
        friend class VisualElement;
        std::string m_Text;
    };

    class KEIRE_API Label final : public TextElement
    {
      public:
        Label() = default;
        explicit Label(std::string text) { SetText(std::move(text)); }

      protected:
        [[nodiscard]] std::string_view TypeName() const noexcept override { return "Label"; }
    };

    class KEIRE_API Image : public VisualElement
    {
      public:
        [[nodiscard]] AssetId Source() const noexcept { return m_Source; }
        void SetSource(AssetId value) noexcept { m_Source = value; }
        [[nodiscard]] Color Tint() const noexcept { return m_Tint; }
        void SetTint(Color value) noexcept { m_Tint = value; }

      protected:
        [[nodiscard]] std::string_view TypeName() const noexcept override { return "Image"; }

      private:
        AssetId m_Source;
        Color m_Tint{1.0F, 1.0F, 1.0F, 1.0F};
    };

    class KEIRE_API Button : public TextElement
    {
      public:
        Button();
        explicit Button(std::function<void()> clicked);
        CallbackToken AddClickedListener(std::function<void()> clicked);
        void Click();

      protected:
        [[nodiscard]] std::string_view TypeName() const noexcept override { return "Button"; }
    };

    template <typename T> class BindableElement : public VisualElement
    {
      public:
        [[nodiscard]] const T& Value() const noexcept { return m_Value; }
        void SetValue(T value) { SetValueInternal(std::move(value), true); }
        void SetValueWithoutNotify(T value) { SetValueInternal(std::move(value), false); }

      protected:
        [[nodiscard]] virtual T Normalize(T value) const { return value; }

      private:
        void SetValueInternal(T value, bool notify)
        {
            value = Normalize(std::move(value));
            if (m_Value == value)
                return;
            T previous = m_Value;
            m_Value = std::move(value);
            try
            {
                WriteBinding("value", m_Value);
            }
            catch (...)
            {
                m_Value = std::move(previous);
                throw;
            }
            if (notify)
            {
                ChangeEvent<T> event;
                event.PreviousValue = previous;
                event.NewValue = m_Value;
                SendEvent(event);
            }
        }

        T m_Value{};
    };

    class KEIRE_API TextField final : public BindableElement<std::string>
    {
      public:
        [[nodiscard]] bool Multiline() const noexcept { return m_Multiline; }
        void SetMultiline(bool value) noexcept { m_Multiline = value; }
        [[nodiscard]] bool Password() const noexcept { return m_Password; }
        void SetPassword(bool value) noexcept { m_Password = value; }
        [[nodiscard]] std::size_t MaximumLength() const noexcept { return m_MaximumLength; }
        void SetMaximumLength(std::size_t value) noexcept { m_MaximumLength = value; }

      protected:
        [[nodiscard]] std::string Normalize(std::string value) const override;
        [[nodiscard]] std::string_view TypeName() const noexcept override { return "TextField"; }

      private:
        std::size_t m_MaximumLength = 0;
        bool m_Multiline = false;
        bool m_Password = false;
    };

    class KEIRE_API Toggle : public BindableElement<bool>
    {
      public:
        [[nodiscard]] std::string_view LabelText() const noexcept { return m_Label; }
        void SetLabelText(std::string value) { m_Label = std::move(value); }

      protected:
        [[nodiscard]] std::string_view TypeName() const noexcept override { return "Toggle"; }

      private:
        std::string m_Label;
    };

    class KEIRE_API Slider final : public BindableElement<float>
    {
      public:
        [[nodiscard]] float LowValue() const noexcept { return m_Low; }
        [[nodiscard]] float HighValue() const noexcept { return m_High; }
        [[nodiscard]] float Step() const noexcept { return m_Step; }
        void SetRange(float low, float high);
        void SetStep(float value);

      protected:
        [[nodiscard]] float Normalize(float value) const override;
        [[nodiscard]] std::string_view TypeName() const noexcept override { return "Slider"; }

      private:
        float m_Low = 0.0F;
        float m_High = 100.0F;
        float m_Step = 0.0F;
    };

    class KEIRE_API ProgressBar final : public BindableElement<float>
    {
      public:
        [[nodiscard]] float LowValue() const noexcept { return m_Low; }
        [[nodiscard]] float HighValue() const noexcept { return m_High; }
        void SetRange(float low, float high);
        [[nodiscard]] std::string_view Title() const noexcept { return m_Title; }
        void SetTitle(std::string value) { m_Title = std::move(value); }

      protected:
        [[nodiscard]] float Normalize(float value) const override;
        [[nodiscard]] std::string_view TypeName() const noexcept override { return "ProgressBar"; }

      private:
        float m_Low = 0.0F;
        float m_High = 100.0F;
        std::string m_Title;
    };

    class KEIRE_API ScrollView : public VisualElement
    {
      public:
        [[nodiscard]] Vector2 ScrollOffset() const noexcept { return m_ScrollOffset; }
        void SetScrollOffset(Vector2 value) noexcept { m_ScrollOffset = value; }

      protected:
        [[nodiscard]] std::string_view TypeName() const noexcept override { return "ScrollView"; }

      private:
        Vector2 m_ScrollOffset;
    };

    class KEIRE_API ListView : public ScrollView
    {
      public:
        using ItemFactory = std::function<Ref<VisualElement>()>;
        using ItemBinder = std::function<void(VisualElement&, std::size_t, const std::any&)>;

        void SetItems(std::vector<std::any> items);
        void SetItemFactory(ItemFactory factory);
        void SetItemBinder(ItemBinder binder);
        void SetOverscan(std::size_t value) noexcept;
        void SetViewport(std::size_t firstVisible, std::size_t visibleCount);
        void RefreshItems();
        [[nodiscard]] std::span<const Ref<VisualElement>> RealizedItems() const noexcept { return m_Realized; }
        [[nodiscard]] std::size_t FirstRealizedIndex() const noexcept { return m_FirstRealized; }

      protected:
        [[nodiscard]] std::string_view TypeName() const noexcept override { return "ListView"; }

      private:
        std::vector<std::any> m_Items;
        std::vector<Ref<VisualElement>> m_Realized;
        ItemFactory m_Factory;
        ItemBinder m_Binder;
        std::size_t m_FirstVisible = 0;
        std::size_t m_VisibleCount = 0;
        std::size_t m_Overscan = 2;
        std::size_t m_FirstRealized = 0;
    };

    class KEIRE_API TreeView final : public ListView
    {
      protected:
        [[nodiscard]] std::string_view TypeName() const noexcept override { return "TreeView"; }
    };

    class KEIRE_API DropdownField final : public BindableElement<std::string>
    {
      public:
        void SetChoices(std::vector<std::string> values);
        [[nodiscard]] std::span<const std::string> Choices() const noexcept { return m_Choices; }

      protected:
        [[nodiscard]] std::string Normalize(std::string value) const override;
        [[nodiscard]] std::string_view TypeName() const noexcept override { return "DropdownField"; }

      private:
        std::vector<std::string> m_Choices;
    };

    class KEIRE_API Foldout final : public Toggle
    {
      public:
        [[nodiscard]] std::string_view Text() const noexcept { return m_Text; }
        void SetText(std::string value) { m_Text = std::move(value); }

      protected:
        [[nodiscard]] std::string_view TypeName() const noexcept override { return "Foldout"; }

      private:
        std::string m_Text;
    };

    class KEIRE_API TabView final : public VisualElement
    {
      public:
        [[nodiscard]] std::optional<std::size_t> SelectedIndex() const noexcept { return m_SelectedIndex; }
        void SetSelectedIndex(std::size_t value);

      protected:
        [[nodiscard]] std::string_view TypeName() const noexcept override { return "TabView"; }

      private:
        std::optional<std::size_t> m_SelectedIndex;
    };

    class KEIRE_API Toolbar final : public VisualElement
    {
      protected:
        [[nodiscard]] std::string_view TypeName() const noexcept override { return "Toolbar"; }
    };

    class KEIRE_API TemplateContainer final : public VisualElement
    {
      public:
        [[nodiscard]] std::string_view TemplateName() const noexcept { return m_TemplateName; }
        void SetTemplateName(std::string value) { m_TemplateName = std::move(value); }

      protected:
        [[nodiscard]] std::string_view TypeName() const noexcept override { return "TemplateContainer"; }

      private:
        std::string m_TemplateName;
    };

    struct UxmlAttributeDescriptor
    {
        std::string Name;
        std::string Type;
    };

    struct UxmlElementDescriptor
    {
        std::string Name;
        std::function<Ref<VisualElement>()> Factory;
        std::vector<UxmlAttributeDescriptor> Attributes;
        std::uint64_t Generation = 0;
    };

    class KEIRE_API UxmlElementRegistry final
    {
      public:
        UxmlElementRegistry() = delete;

        static UxmlElementDescriptor Register(UxmlElementDescriptor descriptor);
        [[nodiscard]] static Ref<VisualElement> Create(std::string_view name);
        [[nodiscard]] static std::vector<UxmlElementDescriptor> Snapshot();
        [[nodiscard]] static std::uint64_t Generation() noexcept;
    };
} // namespace Keire::Ui
