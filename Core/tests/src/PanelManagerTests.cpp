#include <doctest/doctest.h>

#include "Core/Ui/PanelManager.h"

#include <memory>
#include <string>
#include <vector>

namespace
{
    class CountingPanel final : public core::Panel
    {
    public:
        CountingPanel(std::string id, std::string title)
            : core::Panel(std::move(id), std::move(title))
        {
        }

        void OnUpdate(float) override { ++updates; }

        int updates = 0;
    };
}

TEST_CASE("Add registers a panel and allows finding it by identifier")
{
    core::PanelManager panels;

    core::Panel* added = panels.Add(std::make_unique<CountingPanel>("console", "Console"));

    REQUIRE(added != nullptr);
    CHECK(panels.Count() == 1);
    CHECK(panels.Find("console") == added);
    CHECK(added->Title() == "Console");
    CHECK(added->IsVisible());
}

TEST_CASE("Add rejects a duplicate identifier")
{
    core::PanelManager panels;
    panels.Add(std::make_unique<CountingPanel>("console", "Console"));

    core::Panel* duplicate = panels.Add(std::make_unique<CountingPanel>("console", "Other"));

    CHECK(duplicate == nullptr);
    CHECK(panels.Count() == 1);
}

TEST_CASE("Find returns nullptr for an unknown identifier")
{
    core::PanelManager panels;

    CHECK(panels.Find("missing") == nullptr);
}

TEST_CASE("SetVisible changes visibility and signals an unknown identifier")
{
    core::PanelManager panels;
    core::Panel* panel = panels.Add(std::make_unique<CountingPanel>("files", "Files"));

    CHECK(panels.SetVisible("files", false));
    CHECK_FALSE(panel->IsVisible());
    CHECK_FALSE(panels.SetVisible("missing", true));
}

TEST_CASE("OnUpdate reaches all panels, including hidden ones")
{
    core::PanelManager panels;
    auto* visible = static_cast<CountingPanel*>(
        panels.Add(std::make_unique<CountingPanel>("a", "A")));
    auto* hidden = static_cast<CountingPanel*>(
        panels.Add(std::make_unique<CountingPanel>("b", "B")));
    hidden->SetVisible(false);

    panels.OnUpdate(0.016f);

    CHECK(visible->updates == 1);
    CHECK(hidden->updates == 1);
}
