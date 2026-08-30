#include "Keire/Ui/UiElements.h"

#include <cctype>
#include <cmath>
#include <limits>
#include <map>
#include <mutex>
#include <ranges>
#include <stdexcept>

namespace Keire::Ui
{
    namespace
    {
        constexpr std::size_t MaximumChildren = 16'384;
        constexpr std::size_t MaximumListItems = 16'384;
        constexpr std::size_t MaximumListOverscan = 64;
        constexpr std::size_t MaximumClasses = 256;
        constexpr std::size_t MaximumNameBytes = 1024;

        [[nodiscard]] bool ValidToken(const std::string_view value) noexcept
        {
            return !value.empty() && value.size() <= MaximumNameBytes &&
                   std::ranges::none_of(
                       value, [](const char character)
                       { return character == '\0' || std::isspace(static_cast<unsigned char>(character)); });
        }

        [[nodiscard]] bool InteractionEvent(const EventBase& event) noexcept
        {
            return dynamic_cast<const PointerEventBase*>(&event) || dynamic_cast<const KeyDownEvent*>(&event) ||
                   dynamic_cast<const TextInputEvent*>(&event) || dynamic_cast<const NavigationMoveEvent*>(&event) ||
                   dynamic_cast<const SubmitEvent*>(&event);
        }

        template <typename T> [[nodiscard]] const T* AnyValue(const std::any& value) noexcept
        {
            return std::any_cast<T>(&value);
        }

        [[nodiscard]] std::string_view AnyTypeName(const std::any& value) noexcept
        {
            if (!value.has_value())
                return "empty";
            if (value.type() == typeid(std::string))
                return "string";
            if (value.type() == typeid(bool))
                return "bool";
            if (value.type() == typeid(char))
                return "char";
            if (value.type() == typeid(std::int32_t))
                return "int32";
            if (value.type() == typeid(std::uint32_t))
                return "uint32";
            if (value.type() == typeid(std::int64_t))
                return "int64";
            if (value.type() == typeid(std::uint64_t))
                return "uint64";
            if (value.type() == typeid(float))
                return "float";
            if (value.type() == typeid(double))
                return "double";
            return "unsupported";
        }

        class RegistryState final
        {
          public:
            std::mutex Mutex;
            std::map<std::string, UxmlElementDescriptor, std::less<>> Elements;
            std::uint64_t Generation = 0;
        };

        [[nodiscard]] RegistryState& Registry()
        {
            static RegistryState state;
            return state;
        }
    } // namespace

    VisualElement::VisualElement() : m_StableId(AssetId::Generate()) {}

    VisualElement::~VisualElement()
    {
        Clear();
        m_PointerCaptures.clear();
        m_FocusedElement = nullptr;
    }

    void VisualElement::SetStableId(const AssetId value)
    {
        if (!value)
            throw std::invalid_argument("A visual element stable ID cannot be empty.");
        m_StableId = value;
    }

    void VisualElement::SetName(std::string value)
    {
        if (value.size() > MaximumNameBytes || value.find('\0') != std::string::npos)
            throw std::invalid_argument("A visual element name must be at most 1024 bytes and contain no NUL bytes.");
        m_Name = std::move(value);
    }

    bool VisualElement::EnabledInHierarchy() const noexcept
    {
        for (auto* current = this; current; current = current->m_Parent)
        {
            if (!current->m_Enabled)
                return false;
        }
        return true;
    }

    void VisualElement::Add(Ref<VisualElement> child) { Insert(m_Children.size(), std::move(child)); }

    void VisualElement::Insert(std::size_t index, Ref<VisualElement> child)
    {
        if (!child)
            throw std::invalid_argument("A visual element child is required.");
        if (child.Get() == this || child->Contains(this))
            throw std::logic_error("A visual element cannot be parented beneath itself or one of its descendants.");
        if (m_Children.size() >= MaximumChildren && child->m_Parent != this)
            throw std::length_error("A visual element cannot contain more than 16,384 direct children.");

        index = std::min(index, m_Children.size());
        if (child->m_Parent == this)
        {
            const auto existing = std::ranges::find(m_Children, child);
            if (existing == m_Children.end())
                throw std::logic_error("A visual element parent link is inconsistent with its child inventory.");
            const auto previous = static_cast<std::size_t>(std::distance(m_Children.begin(), existing));
            if (previous == index || previous + 1U == index)
                return;
            auto retained = std::move(*existing);
            m_Children.erase(existing);
            if (previous < index)
                --index;
            m_Children.insert(m_Children.begin() + static_cast<std::ptrdiff_t>(index), std::move(retained));
            return;
        }

        child->RemoveFromHierarchy();
        auto* childPointer = child.Get();
        childPointer->m_Parent = this;
        try
        {
            m_Children.insert(m_Children.begin() + static_cast<std::ptrdiff_t>(index), std::move(child));
        }
        catch (...)
        {
            childPointer->m_Parent = nullptr;
            throw;
        }
    }

    bool VisualElement::Remove(const Ref<VisualElement>& child) noexcept
    {
        if (!child)
            return false;
        const auto iterator = std::ranges::find(m_Children, child);
        if (iterator == m_Children.end())
            return false;
        Root().ReleaseOwnedState(iterator->Get());
        (*iterator)->m_Parent = nullptr;
        m_Children.erase(iterator);
        return true;
    }

    void VisualElement::RemoveFromHierarchy() noexcept
    {
        if (!m_Parent)
            return;
        auto* parent = std::exchange(m_Parent, nullptr);
        parent->Root().ReleaseOwnedState(this);
        const auto iterator =
            std::ranges::find_if(parent->m_Children, [this](const auto& child) { return child.Get() == this; });
        if (iterator != parent->m_Children.end())
            parent->m_Children.erase(iterator);
    }

    void VisualElement::Clear() noexcept
    {
        auto& root = Root();
        for (const auto& child : m_Children)
        {
            root.ReleaseOwnedState(child.Get());
            child->m_Parent = nullptr;
        }
        m_Children.clear();
    }

    void VisualElement::AddToClassList(std::string className)
    {
        if (!ValidToken(className))
            throw std::invalid_argument("A visual element class must be a non-empty whitespace-free token.");
        if (!m_Classes.contains(className) && m_Classes.size() >= MaximumClasses)
            throw std::length_error("A visual element cannot contain more than 256 classes.");
        m_Classes.insert(std::move(className));
    }

    bool VisualElement::RemoveFromClassList(const std::string_view className)
    {
        return m_Classes.erase(std::string(className)) != 0U;
    }

    bool VisualElement::ClassListContains(const std::string_view className) const
    {
        return m_Classes.contains(std::string(className));
    }

    std::vector<std::string> VisualElement::ClassList() const
    {
        std::vector<std::string> result(m_Classes.begin(), m_Classes.end());
        std::ranges::sort(result);
        return result;
    }

    void VisualElement::EnableInClassList(std::string className, const bool enabled)
    {
        if (enabled)
            AddToClassList(std::move(className));
        else
            (void)RemoveFromClassList(className);
    }

    void VisualElement::SetEnabled(const bool enabled) noexcept
    {
        if (m_Enabled == enabled)
            return;
        if (!enabled)
            Root().ReleaseOwnedState(this);
        m_Enabled = enabled;
    }

    void VisualElement::SetFocusable(const bool value) noexcept
    {
        if (m_Focusable == value)
            return;
        if (!value && HasFocus())
        {
            try
            {
                Blur();
            }
            catch (...)
            {
                Root().m_FocusedElement = nullptr;
            }
        }
        m_Focusable = value;
    }

    void VisualElement::Focus()
    {
        if (!m_Focusable || !EnabledInHierarchy())
            return;
        auto& root = Root();
        if (root.m_FocusedElement == this)
            return;
        auto* previous = std::exchange(root.m_FocusedElement, nullptr);
        if (previous)
        {
            FocusOutEvent event;
            try
            {
                previous->SendEvent(event);
            }
            catch (...)
            {
                if (!root.m_FocusedElement && root.Contains(previous))
                    root.m_FocusedElement = previous;
                throw;
            }
            if (root.m_FocusedElement)
                return;
        }
        if (&Root() != &root || !EnabledInHierarchy())
            return;
        root.m_FocusedElement = this;
        FocusInEvent event;
        try
        {
            SendEvent(event);
        }
        catch (...)
        {
            if (root.m_FocusedElement == this)
                root.m_FocusedElement = root.Contains(previous) ? previous : nullptr;
            throw;
        }
    }

    void VisualElement::Blur()
    {
        auto& root = Root();
        if (root.m_FocusedElement != this)
            return;
        root.m_FocusedElement = nullptr;
        FocusOutEvent event;
        try
        {
            SendEvent(event);
        }
        catch (...)
        {
            if (!root.m_FocusedElement && root.Contains(this))
                root.m_FocusedElement = this;
            throw;
        }
    }

    bool VisualElement::HasFocus() const noexcept { return Root().m_FocusedElement == this; }

    void VisualElement::CapturePointer(const std::int32_t pointerId)
    {
        if (pointerId < 0)
            throw std::invalid_argument("A UI pointer ID cannot be negative.");
        if (!EnabledInHierarchy())
            throw std::logic_error("A disabled visual element cannot capture a pointer.");
        Root().m_PointerCaptures[pointerId] = this;
    }

    void VisualElement::ReleasePointer(const std::int32_t pointerId) noexcept
    {
        auto& captures = Root().m_PointerCaptures;
        const auto iterator = captures.find(pointerId);
        if (iterator != captures.end() && iterator->second == this)
            captures.erase(iterator);
    }

    bool VisualElement::HasPointerCapture(const std::int32_t pointerId) const noexcept
    {
        return CapturingElement(pointerId) == this;
    }

    VisualElement* VisualElement::CapturingElement(const std::int32_t pointerId) const noexcept
    {
        const auto& captures = Root().m_PointerCaptures;
        const auto iterator = captures.find(pointerId);
        return iterator == captures.end() ? nullptr : iterator->second;
    }

    bool VisualElement::UnregisterCallback(const CallbackToken token) noexcept
    {
        if (!token)
            return false;
        for (auto& [type, callbacks] : m_Callbacks)
        {
            (void)type;
            const auto previous = callbacks.size();
            std::erase_if(callbacks, [token](const CallbackEntry& entry) { return entry.Token == token; });
            if (callbacks.size() != previous)
                return true;
        }
        return false;
    }

    void VisualElement::SendEvent(EventBase& event)
    {
        if (event.m_Phase != PropagationPhase::None)
            throw std::logic_error("A UI event cannot be dispatched recursively while it is already propagating.");
        if (!EnabledInHierarchy() && InteractionEvent(event))
            return;

        event.m_Target = this;
        event.m_CurrentTarget = nullptr;
        event.m_PropagationStopped = false;
        event.m_ImmediatePropagationStopped = false;
        event.m_DefaultPrevented = false;

        std::vector<VisualElement*> path;
        std::vector<Ref<VisualElement>> leases;
        for (auto* current = this; current; current = current->m_Parent)
        {
            path.push_back(current);
            if (!current->m_Parent)
                continue;
            const auto retained = std::ranges::find_if(current->m_Parent->m_Children,
                                                       [current](const auto& child) { return child.Get() == current; });
            if (retained != current->m_Parent->m_Children.end())
                leases.push_back(*retained);
        }

        const auto finish = [&event]() noexcept
        {
            event.m_CurrentTarget = nullptr;
            event.m_Phase = PropagationPhase::None;
        };
        try
        {
            if (event.TricklesDown())
            {
                for (auto iterator = path.rbegin(); iterator != path.rend() && *iterator != this; ++iterator)
                {
                    event.m_CurrentTarget = *iterator;
                    event.m_Phase = PropagationPhase::TrickleDown;
                    (*iterator)->Invoke(event, true);
                    if (event.m_PropagationStopped)
                    {
                        finish();
                        return;
                    }
                }
            }

            event.m_CurrentTarget = this;
            event.m_Phase = PropagationPhase::AtTarget;
            Invoke(event, true);
            if (!event.m_ImmediatePropagationStopped)
                Invoke(event, false);
            if (event.Bubbles() && !event.m_PropagationStopped)
            {
                for (std::size_t index = 1; index < path.size(); ++index)
                {
                    event.m_CurrentTarget = path[index];
                    event.m_Phase = PropagationPhase::BubbleUp;
                    path[index]->Invoke(event, false);
                    if (event.m_PropagationStopped)
                        break;
                }
            }
            finish();
        }
        catch (...)
        {
            finish();
            throw;
        }
    }

    void VisualElement::SetBinding(std::string property, DataBinding binding)
    {
        if (m_ApplyingBindings)
            throw std::logic_error("Bindings cannot be mutated while an atomic binding update is in progress.");
        if (!ValidToken(property))
            throw std::invalid_argument("A binding target property must be a non-empty whitespace-free token.");
        if (binding.SourcePath.empty() || !binding.Read)
            throw std::invalid_argument("A data binding requires a source path and read callback.");
        if (binding.Mode == BindingMode::TwoWay && !binding.Write)
            throw std::invalid_argument("A two-way data binding requires a write callback.");
        if (binding.Apply && (!binding.Capture || !binding.Restore))
        {
            throw std::invalid_argument(
                "A custom binding apply callback requires capture and restore callbacks for atomic rollback.");
        }
        m_Bindings.insert_or_assign(std::move(property), BindingEntry{std::move(binding), false});
    }

    bool VisualElement::RemoveBinding(const std::string_view property) noexcept
    {
        if (m_ApplyingBindings)
            return false;
        return m_Bindings.erase(std::string(property)) != 0U;
    }

    void VisualElement::SetAuthoredBinding(UiBindingDefinition binding)
    {
        if (!ValidToken(binding.Property) || binding.Path.empty() || binding.Path.size() > 1'024 ||
            (binding.Mode != "OneWay" && binding.Mode != "TwoWay" && binding.Mode != "OneTime"))
        {
            throw std::invalid_argument("Authored UI binding metadata is invalid.");
        }
        const auto existing = std::ranges::find(m_AuthoredBindings, binding.Property, &UiBindingDefinition::Property);
        if (existing == m_AuthoredBindings.end())
            m_AuthoredBindings.push_back(std::move(binding));
        else
            *existing = std::move(binding);
    }

    void VisualElement::UpdateBindings()
    {
        struct AppliedBinding final
        {
            std::string Property;
            BindingEntry* Entry = nullptr;
            std::any Snapshot;
        };

        std::vector<std::string> properties;
        properties.reserve(m_Bindings.size());
        for (const auto& [property, entry] : m_Bindings)
        {
            if (entry.Binding.Mode != BindingMode::OneTime || !entry.Applied)
                properties.push_back(property);
        }
        std::ranges::sort(properties);

        std::vector<AppliedBinding> applied;
        applied.reserve(properties.size());
        m_ApplyingBindings = true;
        std::string currentProperty;
        try
        {
            for (const auto& property : properties)
            {
                currentProperty = property;
                auto& entry = m_Bindings.at(property);
                auto value = entry.Binding.Read();
                auto snapshot =
                    entry.Binding.Capture ? entry.Binding.Capture(*this, property) : CaptureBoundProperty(property);
                applied.push_back({property, &entry, std::move(snapshot)});
                std::string error;
                const bool succeeded = entry.Binding.Apply ? entry.Binding.Apply(*this, property, value, error)
                                                           : ApplyBoundProperty(property, value, error);
                if (!succeeded)
                    throw std::runtime_error(error.empty() ? "The target property rejected the binding value." : error);
            }
            for (const auto& value : applied)
            {
                if (value.Entry->Binding.Mode == BindingMode::OneTime)
                    value.Entry->Applied = true;
            }
            m_LastBindingDiagnostic.reset();
            m_ApplyingBindings = false;
        }
        catch (const std::exception& error)
        {
            for (auto iterator = applied.rbegin(); iterator != applied.rend(); ++iterator)
            {
                try
                {
                    if (iterator->Entry->Binding.Restore)
                    {
                        iterator->Entry->Binding.Restore(*this, iterator->Property, iterator->Snapshot);
                    }
                    else
                    {
                        RestoreBoundProperty(iterator->Property, iterator->Snapshot);
                    }
                }
                catch (...)
                {
                }
            }
            m_ApplyingBindings = false;
            const auto binding = m_Bindings.find(currentProperty);
            m_LastBindingDiagnostic = BindingDiagnostic{
                currentProperty, binding == m_Bindings.end() ? std::string{} : binding->second.Binding.SourcePath,
                error.what()};
            throw std::runtime_error("Binding '" + currentProperty + "' from '" + m_LastBindingDiagnostic->SourcePath +
                                     "' failed: " + error.what());
        }
        catch (...)
        {
            for (auto iterator = applied.rbegin(); iterator != applied.rend(); ++iterator)
            {
                try
                {
                    if (iterator->Entry->Binding.Restore)
                        iterator->Entry->Binding.Restore(*this, iterator->Property, iterator->Snapshot);
                    else
                        RestoreBoundProperty(iterator->Property, iterator->Snapshot);
                }
                catch (...)
                {
                }
            }
            m_ApplyingBindings = false;
            const auto binding = m_Bindings.find(currentProperty);
            m_LastBindingDiagnostic = BindingDiagnostic{
                currentProperty, binding == m_Bindings.end() ? std::string{} : binding->second.Binding.SourcePath,
                "Unknown binding callback failure."};
            throw;
        }
    }

    std::optional<BindingDiagnostic> VisualElement::LastBindingDiagnostic() const { return m_LastBindingDiagnostic; }

    void VisualElement::WriteBinding(const std::string_view property, const std::any& value)
    {
        if (m_ApplyingBindings)
            return;
        const auto iterator = m_Bindings.find(std::string(property));
        if (iterator == m_Bindings.end() || iterator->second.Binding.Mode != BindingMode::TwoWay)
            return;
        const auto sourcePath = iterator->second.Binding.SourcePath;
        const auto write = iterator->second.Binding.Write;
        try
        {
            write(value);
            m_LastBindingDiagnostic.reset();
        }
        catch (const std::exception& error)
        {
            m_LastBindingDiagnostic = {std::string(property), sourcePath, error.what()};
            throw std::runtime_error("Two-way binding '" + std::string(property) + "' to '" + sourcePath +
                                     "' failed: " + error.what());
        }
        catch (...)
        {
            m_LastBindingDiagnostic = {std::string(property), sourcePath, "Unknown binding write failure."};
            throw;
        }
    }

    VisualElement& VisualElement::Root() noexcept
    {
        auto* result = this;
        while (result->m_Parent)
            result = result->m_Parent;
        return *result;
    }

    const VisualElement& VisualElement::Root() const noexcept
    {
        auto* result = this;
        while (result->m_Parent)
            result = result->m_Parent;
        return *result;
    }

    bool VisualElement::Contains(const VisualElement* element) const noexcept
    {
        if (element == this)
            return true;
        for (const auto& child : m_Children)
        {
            if (child->Contains(element))
                return true;
        }
        return false;
    }

    void VisualElement::Invoke(EventBase& event, const bool trickle)
    {
        const auto callbackIterator = m_Callbacks.find(std::type_index(typeid(event)));
        if (callbackIterator == m_Callbacks.end())
            return;
        const auto snapshot = callbackIterator->second;
        for (const auto& entry : snapshot)
        {
            if (event.m_ImmediatePropagationStopped)
                return;
            const auto liveType = m_Callbacks.find(std::type_index(typeid(event)));
            if (liveType == m_Callbacks.end())
                return;
            const auto live = std::ranges::find(liveType->second, entry.Token, &CallbackEntry::Token);
            if (live == liveType->second.end() || live->Trickle != trickle)
                continue;
            live->Callback(event);
        }
    }

    void VisualElement::ReleaseOwnedState(const VisualElement* subtree) noexcept
    {
        auto& root = Root();
        if (root.m_FocusedElement && subtree->Contains(root.m_FocusedElement))
        {
            auto* focused = std::exchange(root.m_FocusedElement, nullptr);
            try
            {
                FocusOutEvent event;
                focused->SendEvent(event);
            }
            catch (...)
            {
            }
            if (root.m_FocusedElement && subtree->Contains(root.m_FocusedElement))
                root.m_FocusedElement = nullptr;
        }
        std::erase_if(root.m_PointerCaptures, [subtree](const auto& value) { return subtree->Contains(value.second); });
    }

    void VisualElement::Collect(std::vector<Ref<VisualElement>>& output,
                                const std::function<bool(const VisualElement&)>& filter)
    {
        for (const auto& child : m_Children)
        {
            if (filter(*child))
                output.push_back(child);
            child->Collect(output, filter);
        }
    }

    std::any VisualElement::CaptureBoundProperty(const std::string_view property) const
    {
        if (property == "name")
            return m_Name;
        if (property == "tooltip")
            return m_Tooltip;
        if (property == "text")
        {
            if (const auto* text = dynamic_cast<const TextElement*>(this))
                return std::string(text->Text());
        }
        if (property == "value")
        {
            if (const auto* field = dynamic_cast<const TextField*>(this))
                return field->Value();
            if (const auto* dropdown = dynamic_cast<const DropdownField*>(this))
                return dropdown->Value();
            if (const auto* toggle = dynamic_cast<const Toggle*>(this))
                return toggle->Value();
            if (const auto* slider = dynamic_cast<const Slider*>(this))
                return slider->Value();
            if (const auto* progress = dynamic_cast<const ProgressBar*>(this))
                return progress->Value();
        }
        throw std::invalid_argument("Visual element type '" + std::string(TypeName()) +
                                    "' does not expose bindable property '" + std::string(property) + "'.");
    }

    bool VisualElement::ApplyBoundProperty(const std::string_view property, const std::any& value, std::string& error)
    {
        if (property == "name")
        {
            if (const auto* typed = AnyValue<std::string>(value))
            {
                SetName(*typed);
                return true;
            }
        }
        else if (property == "tooltip")
        {
            if (const auto* typed = AnyValue<std::string>(value))
            {
                SetTooltip(*typed);
                return true;
            }
        }
        else if (property == "text")
        {
            if (auto* text = dynamic_cast<TextElement*>(this))
            {
                if (const auto* typed = AnyValue<std::string>(value))
                {
                    text->SetText(*typed);
                    return true;
                }
            }
        }
        else if (property == "value")
        {
            if (auto* field = dynamic_cast<TextField*>(this))
            {
                if (const auto* typed = AnyValue<std::string>(value))
                {
                    field->SetValueWithoutNotify(*typed);
                    return true;
                }
            }
            else if (auto* dropdown = dynamic_cast<DropdownField*>(this))
            {
                if (const auto* typed = AnyValue<std::string>(value))
                {
                    dropdown->SetValueWithoutNotify(*typed);
                    return true;
                }
            }
            else if (auto* toggle = dynamic_cast<Toggle*>(this))
            {
                if (const auto* typed = AnyValue<bool>(value))
                {
                    toggle->SetValueWithoutNotify(*typed);
                    return true;
                }
            }
            else if (auto* slider = dynamic_cast<Slider*>(this))
            {
                if (const auto* typed = AnyValue<float>(value))
                {
                    slider->SetValueWithoutNotify(*typed);
                    return true;
                }
            }
            else if (auto* progress = dynamic_cast<ProgressBar*>(this))
            {
                if (const auto* typed = AnyValue<float>(value))
                {
                    progress->SetValueWithoutNotify(*typed);
                    return true;
                }
            }
        }
        error =
            "Property '" + std::string(property) + "' rejected value type '" + std::string(AnyTypeName(value)) + "'.";
        return false;
    }

    void VisualElement::RestoreBoundProperty(const std::string_view property, const std::any& value)
    {
        std::string error;
        if (!ApplyBoundProperty(property, value, error))
            throw std::logic_error("Binding rollback failed: " + error);
    }

    void TextElement::SetText(std::string value)
    {
        if (m_Text == value)
            return;
        auto previous = m_Text;
        m_Text = std::move(value);
        try
        {
            WriteBinding("text", m_Text);
        }
        catch (...)
        {
            m_Text = std::move(previous);
            throw;
        }
    }

    Button::Button() { SetFocusable(true); }

    Button::Button(std::function<void()> clicked) : Button() { (void)AddClickedListener(std::move(clicked)); }

    CallbackToken Button::AddClickedListener(std::function<void()> clicked)
    {
        if (!clicked)
            throw std::invalid_argument("A button click listener is required.");
        return RegisterCallback<ClickEvent>([clicked = std::move(clicked)](ClickEvent&) { clicked(); });
    }

    void Button::Click()
    {
        if (!EnabledInHierarchy())
            return;
        ClickEvent event;
        SendEvent(event);
    }

    std::string TextField::Normalize(std::string value) const
    {
        if (!m_Multiline)
        {
            std::erase(value, '\r');
            std::replace(value.begin(), value.end(), '\n', ' ');
        }
        if (m_MaximumLength != 0U && value.size() > m_MaximumLength)
            value.resize(m_MaximumLength);
        return value;
    }

    void Slider::SetRange(const float low, const float high)
    {
        if (!std::isfinite(low) || !std::isfinite(high) || high < low)
            throw std::invalid_argument("A slider range must be finite and ordered from low to high.");
        const auto previousLow = m_Low;
        const auto previousHigh = m_High;
        m_Low = low;
        m_High = high;
        try
        {
            SetValueWithoutNotify(Value());
        }
        catch (...)
        {
            m_Low = previousLow;
            m_High = previousHigh;
            throw;
        }
    }

    void Slider::SetStep(const float value)
    {
        if (!std::isfinite(value) || value < 0.0F)
            throw std::invalid_argument("A slider step must be finite and non-negative.");
        m_Step = value;
        SetValueWithoutNotify(Value());
    }

    float Slider::Normalize(float value) const
    {
        if (!std::isfinite(value))
            value = m_Low;
        value = std::clamp(value, m_Low, m_High);
        if (m_Step > 0.0F)
            value = m_Low + std::round((value - m_Low) / m_Step) * m_Step;
        return std::clamp(value, m_Low, m_High);
    }

    void ProgressBar::SetRange(const float low, const float high)
    {
        if (!std::isfinite(low) || !std::isfinite(high) || high < low)
            throw std::invalid_argument("A progress-bar range must be finite and ordered from low to high.");
        const auto previousLow = m_Low;
        const auto previousHigh = m_High;
        m_Low = low;
        m_High = high;
        try
        {
            SetValueWithoutNotify(Value());
        }
        catch (...)
        {
            m_Low = previousLow;
            m_High = previousHigh;
            throw;
        }
    }

    float ProgressBar::Normalize(const float value) const
    {
        return std::clamp(std::isfinite(value) ? value : m_Low, m_Low, m_High);
    }

    void ListView::SetItems(std::vector<std::any> items)
    {
        if (items.size() > MaximumListItems)
            throw std::length_error("A ListView cannot contain more than 16,384 source items.");
        m_Items = std::move(items);
    }

    void ListView::SetItemFactory(ItemFactory factory)
    {
        if (!factory)
            throw std::invalid_argument("A ListView item factory is required.");
        m_Factory = std::move(factory);
    }

    void ListView::SetItemBinder(ItemBinder binder)
    {
        if (!binder)
            throw std::invalid_argument("A ListView item binder is required.");
        m_Binder = std::move(binder);
    }

    void ListView::SetOverscan(const std::size_t value) noexcept { m_Overscan = std::min(value, MaximumListOverscan); }

    void ListView::SetViewport(const std::size_t firstVisible, const std::size_t visibleCount)
    {
        if (visibleCount > MaximumListItems)
            throw std::length_error("A ListView viewport cannot request more than 16,384 visible items.");
        m_FirstVisible = firstVisible;
        m_VisibleCount = visibleCount;
    }

    void ListView::RefreshItems()
    {
        const auto clampedFirstVisible = std::min(m_FirstVisible, m_Items.size());
        const auto first = clampedFirstVisible > m_Overscan ? clampedFirstVisible - m_Overscan : 0U;
        const auto visibleEnd = clampedFirstVisible + std::min(m_VisibleCount, m_Items.size() - clampedFirstVisible);
        const auto end = std::min(m_Items.size(), visibleEnd + m_Overscan);
        std::vector<Ref<VisualElement>> candidate;
        candidate.reserve(end - first);
        for (std::size_t index = first; index < end; ++index)
        {
            auto element = m_Factory ? m_Factory() : CreateRef<VisualElement>();
            if (!element)
                throw std::runtime_error("The ListView item factory returned an empty element.");
            if (element->Parent())
                throw std::logic_error("A ListView item factory must return an unparented element.");
            if (m_Binder)
                m_Binder(*element, index, m_Items[index]);
            else
                element->SetUserData(m_Items[index]);
            candidate.push_back(std::move(element));
        }

        auto previous = m_Realized;
        const auto previousFirst = m_FirstRealized;
        Clear();
        try
        {
            for (const auto& item : candidate)
                Add(item);
            m_Realized = std::move(candidate);
            m_FirstRealized = first;
        }
        catch (...)
        {
            Clear();
            for (const auto& item : previous)
                Add(item);
            m_Realized = std::move(previous);
            m_FirstRealized = previousFirst;
            throw;
        }
    }

    void DropdownField::SetChoices(std::vector<std::string> values)
    {
        if (values.size() > 256U)
            throw std::length_error("A DropdownField cannot contain more than 256 choices.");
        std::unordered_set<std::string> unique;
        for (const auto& value : values)
        {
            if (!unique.insert(value).second)
                throw std::invalid_argument("A DropdownField cannot contain duplicate choices.");
        }
        auto previous = m_Choices;
        m_Choices = std::move(values);
        try
        {
            SetValueWithoutNotify(Value());
        }
        catch (...)
        {
            m_Choices = std::move(previous);
            throw;
        }
    }

    std::string DropdownField::Normalize(std::string value) const
    {
        if (m_Choices.empty())
            return {};
        return std::ranges::find(m_Choices, value) == m_Choices.end() ? m_Choices.front() : value;
    }

    void TabView::SetSelectedIndex(const std::size_t value)
    {
        if (value >= Children().size())
            throw std::out_of_range("A TabView selected index must name an existing child.");
        m_SelectedIndex = value;
    }

    UxmlElementDescriptor UxmlElementRegistry::Register(UxmlElementDescriptor descriptor)
    {
        if (!ValidToken(descriptor.Name) || !descriptor.Factory)
            throw std::invalid_argument("A UXML element registration requires a valid name and factory.");
        std::unordered_set<std::string> attributes;
        for (const auto& attribute : descriptor.Attributes)
        {
            if (!ValidToken(attribute.Name) || attribute.Type.empty() || !attributes.insert(attribute.Name).second)
                throw std::invalid_argument("UXML attribute names must be valid and unique within their element.");
        }
        auto& registry = Registry();
        std::scoped_lock lock(registry.Mutex);
        if (registry.Generation == (std::numeric_limits<std::uint64_t>::max)())
            throw std::overflow_error("The UXML element registry generation is exhausted.");
        descriptor.Generation = ++registry.Generation;
        registry.Elements.insert_or_assign(descriptor.Name, descriptor);
        return descriptor;
    }

    Ref<VisualElement> UxmlElementRegistry::Create(const std::string_view name)
    {
        std::function<Ref<VisualElement>()> factory;
        {
            auto& registry = Registry();
            std::scoped_lock lock(registry.Mutex);
            const auto iterator = registry.Elements.find(name);
            if (iterator == registry.Elements.end())
                return {};
            factory = iterator->second.Factory;
        }
        auto result = factory();
        if (!result)
            throw std::runtime_error("The registered UXML element factory returned an empty element.");
        return result;
    }

    std::vector<UxmlElementDescriptor> UxmlElementRegistry::Snapshot()
    {
        auto& registry = Registry();
        std::scoped_lock lock(registry.Mutex);
        std::vector<UxmlElementDescriptor> result;
        result.reserve(registry.Elements.size());
        for (const auto& [name, descriptor] : registry.Elements)
        {
            (void)name;
            result.push_back(descriptor);
        }
        return result;
    }

    std::uint64_t UxmlElementRegistry::Generation() noexcept
    {
        auto& registry = Registry();
        std::scoped_lock lock(registry.Mutex);
        return registry.Generation;
    }
} // namespace Keire::Ui
