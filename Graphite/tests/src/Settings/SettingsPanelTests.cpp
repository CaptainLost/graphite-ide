#include <doctest/doctest.h>

#include "Core/Events/KeyEvent.h"
#include "Settings/SettingsPanel.h"

#include <memory>
#include <string_view>
#include <vector>

namespace {

	class CountingSection final : public Graphite::SettingsSection
	{
	public:
		explicit CountingSection(std::string_view title) : m_Title(title) {}

		std::string_view GetTitle() const override { return m_Title; }

		void OnImGuiRender() override {}

		void OnEvent(Core::Event&) override { ++m_Events; }

		void OnDeactivated() override { ++m_Deactivations; }

		std::size_t GetEventCount() const { return m_Events; }

		std::size_t GetDeactivationCount() const { return m_Deactivations; }

	private:
		std::string_view m_Title;
		std::size_t m_Events = 0;
		std::size_t m_Deactivations = 0;
	};

	struct Fixture
	{
		CountingSection* First = nullptr;
		CountingSection* Second = nullptr;
		Graphite::SettingsPanel Panel;

		Fixture() : Panel(Build()) {}

		std::vector<Core::Scope<Graphite::SettingsSection>> Build()
		{
			auto first = Core::CreateScope<CountingSection>("First");
			auto second = Core::CreateScope<CountingSection>("Second");

			First = first.get();
			Second = second.get();

			std::vector<Core::Scope<Graphite::SettingsSection>> sections;
			sections.push_back(std::move(first));
			sections.push_back(std::move(second));

			return sections;
		}
	};
}

TEST_CASE("SettingsPanel opens on its first section")
{
	Fixture fixture;

	CHECK(fixture.Panel.GetActiveSection() == 0);
}

// The rule that keeps the key binding editor from swallowing keystrokes aimed at a section the
// user is actually looking at.
TEST_CASE("SettingsPanel offers an event to the active section only")
{
	Fixture fixture;

	Core::KeyPressedEvent event{Core::KeyCode::A, Core::KeyModifier::None, false};
	fixture.Panel.OnEvent(event);

	CHECK(fixture.First->GetEventCount() == 1);
	CHECK(fixture.Second->GetEventCount() == 0);

	fixture.Panel.SelectSection(1);
	fixture.Panel.OnEvent(event);

	CHECK(fixture.First->GetEventCount() == 1);
	CHECK(fixture.Second->GetEventCount() == 1);
}

// A section index from a stale command or a shrunken list must not index past the end.
TEST_CASE("SettingsPanel ignores a section index past the end")
{
	Fixture fixture;

	fixture.Panel.SelectSection(99);

	CHECK(fixture.Panel.GetActiveSection() == 0);
}

TEST_CASE("SettingsPanel with no sections answers without indexing anything")
{
	Graphite::SettingsPanel panel{{}};

	Core::KeyPressedEvent event{Core::KeyCode::A, Core::KeyModifier::None, false};
	panel.OnEvent(event);

	CHECK(panel.GetActiveSection() == 0);
}

TEST_CASE("SettingsPanel tells a section it is being left")
{
	Fixture fixture;

	fixture.Panel.SelectSection(1);

	CHECK(fixture.First->GetDeactivationCount() == 1);
	CHECK(fixture.Second->GetDeactivationCount() == 0);
}

// Selecting the section already showing is not a change and must not drop its state — a user
// clicking the highlighted entry would otherwise lose a capture in progress.
TEST_CASE("SettingsPanel does not deactivate a section reselected")
{
	Fixture fixture;

	fixture.Panel.SelectSection(0);

	CHECK(fixture.First->GetDeactivationCount() == 0);
}
