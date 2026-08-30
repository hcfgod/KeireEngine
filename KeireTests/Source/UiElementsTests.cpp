#include "Keire/Ui/UiElements.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <any>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    class StatusElement final : public Keire::Ui::VisualElement
    {
      protected:
        [[nodiscard]] std::string_view TypeName() const noexcept override { return "StatusElement"; }
    };
} // namespace

TEST_CASE("Retained visual elements own children without cycles and query typed descendants")
{
    auto root = Keire::CreateRef<Keire::Ui::VisualElement>();
    auto panel = Keire::CreateRef<Keire::Ui::VisualElement>();
    auto label = Keire::CreateRef<Keire::Ui::Label>("Ready");
    label->SetName("status");
    label->AddToClassList("primary");
    panel->Add(label);
    root->Add(panel);

    CHECK(label->Parent() == panel.Get());
    CHECK(root->Q<Keire::Ui::Label>("status", "primary") == label);
    CHECK(root->Query<Keire::Ui::TextElement>().ToList().size() == 1U);
    CHECK_THROWS_WITH_AS(panel->Add(root),
                         "A visual element cannot be parented beneath itself or one of its descendants.",
                         std::logic_error);

    Keire::WeakRef<Keire::Ui::Label> lifetime(label);
    label.Reset();
    panel->Clear();
    CHECK(lifetime.Expired());
    CHECK(root->Query<Keire::Ui::Label>().ToList().empty());
}

TEST_CASE("Retained UI events traverse capture target and bubble phases deterministically")
{
    auto root = Keire::CreateRef<Keire::Ui::VisualElement>();
    auto panel = Keire::CreateRef<Keire::Ui::VisualElement>();
    auto button = Keire::CreateRef<Keire::Ui::Button>();
    root->Add(panel);
    panel->Add(button);

    std::vector<std::string> order;
    (void)root->RegisterCallback<Keire::Ui::ClickEvent>(
        [&](Keire::Ui::ClickEvent& event)
        {
            CHECK(event.Phase() == Keire::Ui::PropagationPhase::TrickleDown);
            CHECK(event.Target() == button.Get());
            order.emplace_back("root-capture");
        },
        Keire::Ui::TrickleDown::Yes);
    (void)panel->RegisterCallback<Keire::Ui::ClickEvent>(
        [&](Keire::Ui::ClickEvent&) { order.emplace_back("panel-capture"); }, Keire::Ui::TrickleDown::Yes);
    (void)button->RegisterCallback<Keire::Ui::ClickEvent>(
        [&](Keire::Ui::ClickEvent& event)
        {
            CHECK(event.Phase() == Keire::Ui::PropagationPhase::AtTarget);
            order.emplace_back("target-capture");
        },
        Keire::Ui::TrickleDown::Yes);
    (void)button->RegisterCallback<Keire::Ui::ClickEvent>([&](Keire::Ui::ClickEvent&)
                                                          { order.emplace_back("target"); });
    (void)panel->RegisterCallback<Keire::Ui::ClickEvent>([&](Keire::Ui::ClickEvent&)
                                                         { order.emplace_back("panel-bubble"); });
    (void)root->RegisterCallback<Keire::Ui::ClickEvent>(
        [&](Keire::Ui::ClickEvent& event)
        {
            CHECK(event.Phase() == Keire::Ui::PropagationPhase::BubbleUp);
            order.emplace_back("root-bubble");
        });

    button->Click();
    CHECK(order == std::vector<std::string>{"root-capture", "panel-capture", "target-capture", "target", "panel-bubble",
                                            "root-bubble"});
}

TEST_CASE("Callback mutation and immediate propagation stopping are safe during dispatch")
{
    auto button = Keire::CreateRef<Keire::Ui::Button>();
    std::size_t calls = 0;
    Keire::Ui::CallbackToken later;
    (void)button->RegisterCallback<Keire::Ui::ClickEvent>(
        [&](Keire::Ui::ClickEvent& event)
        {
            ++calls;
            if (later)
            {
                CHECK(button->UnregisterCallback(later));
                later = {};
            }
            event.StopImmediatePropagation();
        });
    later = button->RegisterCallback<Keire::Ui::ClickEvent>([&](Keire::Ui::ClickEvent&) { calls += 100U; });

    button->Click();
    CHECK(calls == 1U);
    button->Click();
    CHECK(calls == 2U);
}

TEST_CASE("Disabling a retained subtree blocks interaction and releases focus and pointer capture")
{
    auto root = Keire::CreateRef<Keire::Ui::VisualElement>();
    auto panel = Keire::CreateRef<Keire::Ui::VisualElement>();
    auto button = Keire::CreateRef<Keire::Ui::Button>();
    root->Add(panel);
    panel->Add(button);
    std::size_t clicks = 0;
    (void)button->AddClickedListener([&] { ++clicks; });

    button->Focus();
    button->CapturePointer(7);
    REQUIRE(button->HasFocus());
    REQUIRE(button->HasPointerCapture(7));
    panel->SetEnabled(false);
    CHECK_FALSE(button->HasFocus());
    CHECK_FALSE(button->HasPointerCapture(7));
    button->Click();
    CHECK(clicks == 0U);
    CHECK_THROWS_WITH_AS(button->CapturePointer(7), "A disabled visual element cannot capture a pointer.",
                         std::logic_error);

    panel->SetEnabled(true);
    button->Click();
    CHECK(clicks == 1U);
}

TEST_CASE("Binding updates roll back every earlier target mutation and publish exact diagnostics")
{
    auto label = Keire::CreateRef<Keire::Ui::Label>("previous");
    label->SetName("before");
    label->SetBinding("name", {.SourcePath = "model.name", .Read = [] { return std::any(std::string("candidate")); }});
    label->SetBinding("text", {.SourcePath = "model.status", .Read = [] { return std::any(42); }});

    CHECK_THROWS_WITH_AS(label->UpdateBindings(),
                         "Binding 'text' from 'model.status' failed: Property 'text' rejected value type 'int32'.",
                         std::runtime_error);
    CHECK(label->Name() == "before");
    CHECK(label->Text() == "previous");
    const auto diagnostic = label->LastBindingDiagnostic();
    REQUIRE(diagnostic);
    CHECK(diagnostic->TargetProperty == "text");
    CHECK(diagnostic->SourcePath == "model.status");
    CHECK(diagnostic->Message.find("rejected value type") != std::string::npos);

    CHECK_THROWS_WITH_AS(label->SetBinding("custom", {.SourcePath = "model.custom",
                                                      .Read = [] { return std::any(1); },
                                                      .Apply = [](Keire::Ui::VisualElement&, std::string_view,
                                                                  const std::any&, std::string&) { return true; }}),
                         "A custom binding apply callback requires capture and restore callbacks for atomic rollback.",
                         std::invalid_argument);
}

TEST_CASE("Two-way control binding failures preserve the last valid value")
{
    auto toggle = Keire::CreateRef<Keire::Ui::Toggle>();
    bool model = false;
    toggle->SetBinding("value", {.SourcePath = "model.enabled",
                                 .Mode = Keire::Ui::BindingMode::TwoWay,
                                 .Read = [&] { return std::any(model); },
                                 .Write = [](const std::any&) { throw std::runtime_error("model rejected write"); }});
    toggle->UpdateBindings();
    CHECK_FALSE(toggle->Value());
    CHECK_THROWS_WITH_AS(toggle->SetValue(true),
                         "Two-way binding 'value' to 'model.enabled' failed: model rejected write", std::runtime_error);
    CHECK_FALSE(toggle->Value());
    REQUIRE(toggle->LastBindingDiagnostic());
}

TEST_CASE("Core retained controls normalize values without emitting redundant changes")
{
    auto slider = Keire::CreateRef<Keire::Ui::Slider>();
    slider->SetRange(-1.0F, 1.0F);
    slider->SetStep(0.25F);
    std::size_t changes = 0;
    (void)slider->RegisterCallback<Keire::Ui::ChangeEvent<float>>(
        [&](Keire::Ui::ChangeEvent<float>& event)
        {
            ++changes;
            CHECK(event.PreviousValue == doctest::Approx(0.0F));
            CHECK(event.NewValue == doctest::Approx(0.75F));
        });
    slider->SetValue(0.68F);
    slider->SetValue(0.74F);
    CHECK(slider->Value() == doctest::Approx(0.75F));
    CHECK(changes == 1U);

    auto field = Keire::CreateRef<Keire::Ui::TextField>();
    field->SetMaximumLength(4U);
    field->SetValue("ab\ncd");
    CHECK(field->Value() == "ab c");

    auto dropdown = Keire::CreateRef<Keire::Ui::DropdownField>();
    dropdown->SetChoices({"Low", "High"});
    dropdown->SetValue("unknown");
    CHECK(dropdown->Value() == "Low");
    CHECK_THROWS_AS(dropdown->SetChoices({"Same", "Same"}), std::invalid_argument);
}

TEST_CASE("ListView realizes only the visible range plus bounded overscan and commits refresh atomically")
{
    auto list = Keire::CreateRef<Keire::Ui::ListView>();
    std::vector<std::any> items;
    for (std::size_t index = 0; index < 100U; ++index)
        items.emplace_back(index);
    list->SetItems(std::move(items));
    list->SetOverscan(2U);
    list->SetViewport(10U, 5U);
    list->SetItemFactory([] { return Keire::CreateRef<Keire::Ui::Label>(); });
    list->SetItemBinder([](Keire::Ui::VisualElement& element, const std::size_t index, const std::any&)
                        { element.SetName("row-" + std::to_string(index)); });
    list->RefreshItems();

    REQUIRE(list->RealizedItems().size() == 9U);
    CHECK(list->FirstRealizedIndex() == 8U);
    CHECK(list->RealizedItems().front()->Name() == "row-8");
    CHECK(list->RealizedItems().back()->Name() == "row-16");

    const auto previousFirst = list->RealizedItems().front();
    list->SetItemBinder(
        [](Keire::Ui::VisualElement&, const std::size_t index, const std::any&)
        {
            if (index == 12U)
                throw std::runtime_error("intentional binder failure");
        });
    CHECK_THROWS_WITH_AS(list->RefreshItems(), "intentional binder failure", std::runtime_error);
    CHECK(list->RealizedItems().front() == previousFirst);
    CHECK(list->Children().size() == 9U);
}

TEST_CASE("UXML custom-element registrations publish immutable generation-stamped snapshots")
{
    const auto name = "Keire.StatusElement." + Keire::AssetId::Generate().ToString();
    const auto before = Keire::Ui::UxmlElementRegistry::Generation();
    const auto registered =
        Keire::Ui::UxmlElementRegistry::Register({.Name = name,
                                                  .Factory = [] { return Keire::CreateRef<StatusElement>(); },
                                                  .Attributes = {{"message", "string"}, {"severity", "enum"}}});
    CHECK(registered.Generation == before + 1U);
    CHECK(Keire::Ui::UxmlElementRegistry::Generation() == registered.Generation);
    const auto created = Keire::Ui::UxmlElementRegistry::Create(name);
    REQUIRE(created);
    CHECK(created->Type() == "StatusElement");

    const auto snapshot = Keire::Ui::UxmlElementRegistry::Snapshot();
    const auto match = std::ranges::find(snapshot, name, &Keire::Ui::UxmlElementDescriptor::Name);
    REQUIRE(match != snapshot.end());
    CHECK(match->Generation == registered.Generation);
    CHECK(match->Attributes.size() == 2U);
    CHECK_FALSE(Keire::Ui::UxmlElementRegistry::Create("Keire.DoesNotExist"));
}
