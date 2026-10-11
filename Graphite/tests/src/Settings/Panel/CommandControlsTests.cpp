#include <doctest/doctest.h>

#include "Settings/Panel/CommandControls.h"

#include "Core/Command/Command.h"
#include "Core/Command/CommandRegistry.h"

#include <array>
#include <string>

namespace {

	struct Displays
	{
		std::string Current = "dim";
		Core::CommandRegistry Commands;

		Displays()
		{
			Add("view.show", "show");
			Add("view.dim", "dim");
		}

		void Add(std::string id, std::string display)
		{
			Commands.Add(Core::Command{.Id = std::move(id),
									   .Label = display,
									   .Run = [this, display] { Current = display; },
									   .Checked = [this, display] { return Current == display; }});
		}
	};

	const std::array<Graphite::CommandChoice, 3> kChoices{Graphite::CommandChoice{.Label = "Show", .CommandId = "view.show"},
														  Graphite::CommandChoice{.Label = "Dim", .CommandId = "view.dim"},
														  Graphite::CommandChoice{.Label = "Gone", .CommandId = "view.missing"}};
}

TEST_CASE("The checked choice is the one whose command says it is checked")
{
	Displays displays;

	CHECK(Graphite::CheckedChoice(displays.Commands, kChoices) == 1);

	displays.Current = "nothing";
	CHECK(Graphite::CheckedChoice(displays.Commands, kChoices) == kChoices.size());
}

TEST_CASE("Running a choice runs its command, and a choice with no command does nothing")
{
	Displays displays;

	Graphite::RunChoice(displays.Commands, kChoices[0]);
	CHECK(displays.Current == "show");

	Graphite::RunChoice(displays.Commands, kChoices[2]);
	CHECK(displays.Current == "show");
}

TEST_CASE("A choice's labels come in its order")
{
	CHECK(Graphite::LabelsOf(kChoices) == std::vector<std::string>{"Show", "Dim", "Gone"});
}
