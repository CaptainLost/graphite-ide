#include <doctest/doctest.h>

#include "Language/Diagnostics/ProblemsPanel.h"
#include "UI/FileIcons.h"
#include "UI/Lists/ListRow.h"
#include "Workspace/Workspace.h"

#include "DrawnColours.h"
#include "ImGuiFrames.h"
#include "TempAppSettings.h"
#include "TempDirectory.h"

#include "Core/Events/EventBus.h"
#include "Core/Text/DocumentSet.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cmath>
#include <filesystem>
#include <optional>
#include <string_view>

namespace {

	constexpr ImVec2 kPanelSize{400.0f, 300.0f};
	constexpr float kInterfaceItemSpacing = 5.0f;

	const ImGuiWindow* FindProblemList()
	{
		constexpr std::string_view kProblemListName = "##problemList";

		for (const ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
		{
			if (std::string_view{window->Name}.find(kProblemListName) != std::string_view::npos)
			{
				return window;
			}
		}

		return nullptr;
	}

	Graphite::DiagnosticsUpdate OneErrorIn(const std::filesystem::path& path)
	{
		constexpr int kFirstVersion = 1;

		Graphite::Diagnostic error;
		error.Severity = Graphite::DiagnosticSeverity::Error;
		error.Message = "Unexpected end of input";

		Graphite::DiagnosticsUpdate update;
		update.Published.push_back(
			Graphite::PublishedDiagnostics{.Path = path, .Version = Graphite::DocumentVersion{kFirstVersion}, .Items = {error}});

		return update;
	}
}

TEST_CASE("A file in Problems draws its badge in its kind's colour and its name in the text colour past it")
{
	constexpr bool kDarkScheme = false;

	TempDirectory folder;
	const std::filesystem::path settingsFile = folder.MakeFile("graphite.json", "{}");

	ImGuiFrames frames;
	TempAppSettings settings;
	Core::EventBus events;
	Graphite::Workspace workspace{events, settings.Get()};
	Core::DocumentSet documents;
	REQUIRE(documents.Open(settingsFile, Core::TabKind::Permanent) == Core::OpenOutcome::Opened);

	Graphite::DiagnosticsModel diagnostics;
	diagnostics.Apply(OneErrorIn(settingsFile));

	const Graphite::ActiveScheme scheme;
	Graphite::ProblemsPanel panel{diagnostics, documents, workspace, scheme};

	frames.Next([&] {
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(kPanelSize);
		ImGui::Begin("Problems", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings);
		panel.OnImGuiRender();
		ImGui::End();
	});

	const ImGuiWindow* list = FindProblemList();
	REQUIRE(list != nullptr);

	const std::optional<HorizontalExtent> glyph =
		HorizontalExtentInColour(*list->DrawList, Graphite::ColourOf(Graphite::FileKind::Json, kDarkScheme));
	const std::optional<HorizontalExtent> name =
		HorizontalExtentInColour(*list->DrawList, ImGui::GetColorU32(ImGuiCol_Text));

	REQUIRE(glyph.has_value());
	REQUIRE(name.has_value());
	CHECK(glyph->Right < name->Left);
}

TEST_CASE("A file heading in Problems takes one list row pitch, as the problem rows under it do")
{
	constexpr bool kDarkScheme = false;

	TempDirectory folder;
	const std::filesystem::path notesFile = folder.MakeFile("README.md", "# Notes");
	const std::filesystem::path sourceFile = folder.MakeFile("main.cpp", "int main() {}");

	ImGuiFrames frames;
	TempAppSettings settings;
	Core::EventBus events;
	Graphite::Workspace workspace{events, settings.Get()};
	Core::DocumentSet documents;
	REQUIRE(documents.Open(notesFile, Core::TabKind::Permanent) == Core::OpenOutcome::Opened);
	REQUIRE(documents.Open(sourceFile, Core::TabKind::Permanent) == Core::OpenOutcome::Opened);

	Graphite::DiagnosticsModel diagnostics;
	diagnostics.Apply(OneErrorIn(notesFile));
	diagnostics.Apply(OneErrorIn(sourceFile));

	const Graphite::ActiveScheme scheme;
	Graphite::ProblemsPanel panel{diagnostics, documents, workspace, scheme};

	float pitch = 0.0f;
	ImGui::GetStyle().ItemSpacing.y = kInterfaceItemSpacing;

	frames.Next([&] {
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(kPanelSize);
		ImGui::Begin("Problems", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings);
		panel.OnImGuiRender();
		pitch = Graphite::ListRowPitch();
		ImGui::End();
	});

	const ImGuiWindow* list = FindProblemList();
	REQUIRE(list != nullptr);

	// WARNING: Both badges are capitals. A badge of other characters, such as braces, starts at another height.
	const std::optional<VerticalExtent> notesBadge =
		VerticalExtentInColour(*list->DrawList, Graphite::ColourOf(Graphite::FileKind::Markdown, kDarkScheme));
	const std::optional<VerticalExtent> sourceBadge =
		VerticalExtentInColour(*list->DrawList, Graphite::ColourOf(Graphite::FileKind::CppSource, kDarkScheme));

	REQUIRE(notesBadge.has_value());
	REQUIRE(sourceBadge.has_value());

	constexpr float kHeadingAndOneProblem = 2.0f;
	CHECK(std::abs(sourceBadge->Top - notesBadge->Top) == doctest::Approx(kHeadingAndOneProblem * pitch));
}

TEST_CASE("A problem's highlight is inset six pixels from the Problems panel's edge")
{
	constexpr float kHighlightInset = 6.0f;
	constexpr float kFringe = 1.0f;
	constexpr float kIntoTheProblemRow = 1.5f;

	TempDirectory folder;
	const std::filesystem::path settingsFile = folder.MakeFile("graphite.json", "{}");

	ImGuiFrames frames;
	TempAppSettings settings;
	Core::EventBus events;
	Graphite::Workspace workspace{events, settings.Get()};
	Core::DocumentSet documents;
	REQUIRE(documents.Open(settingsFile, Core::TabKind::Permanent) == Core::OpenOutcome::Opened);

	Graphite::DiagnosticsModel diagnostics;
	diagnostics.Apply(OneErrorIn(settingsFile));

	const Graphite::ActiveScheme scheme;
	Graphite::ProblemsPanel panel{diagnostics, documents, workspace, scheme};

	REQUIRE(panel.GetSides() == Core::Panel::Sides::Flush);

	float pitch = 0.0f;

	// As the panel manager begins a flush panel's window: with no padding at its sides.
	const auto draw = [&] {
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, ImGui::GetStyle().WindowPadding.y));
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(kPanelSize);
		ImGui::Begin("Problems", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings);
		ImGui::PopStyleVar();
		panel.OnImGuiRender();
		pitch = Graphite::ListRowPitch();
		ImGui::End();
	};

	frames.Next(draw);

	const ImGuiWindow* list = FindProblemList();
	REQUIRE(list != nullptr);

	// WARNING: A row that allows overlap is hovered only from the frame after the pointer reaches it. By then the
	// keyboard's focus, which lands on the only problem when the list appears, has selected it, and a selected row
	// keeps the accent under the pointer, so it is highlighted in one colour or the other.
	ImGui::GetIO().AddMousePosEvent(list->Pos.x + kPanelSize.x * 0.5f, list->Pos.y + pitch * kIntoTheProblemRow);
	frames.Next(draw);
	frames.Next(draw);

	list = FindProblemList();
	REQUIRE(list != nullptr);

	const Graphite::ListRowColours colours = Graphite::CurrentListRowColours();
	std::optional<HorizontalExtent> highlight = HorizontalExtentInColour(*list->DrawList, ImGui::GetColorU32(colours.Hovered));

	if (!highlight.has_value())
	{
		highlight = HorizontalExtentInColour(*list->DrawList, ImGui::GetColorU32(colours.Selected));
	}

	REQUIRE(highlight.has_value());
	CHECK(std::abs(highlight->Left - kHighlightInset) <= kFringe);
	CHECK(std::abs(highlight->Right - (kPanelSize.x - kHighlightInset)) <= kFringe);
}

TEST_CASE("A file heading in Problems stands a list row's end inset in from the panel's edge")
{
	constexpr bool kDarkScheme = false;

	TempDirectory folder;
	const std::filesystem::path settingsFile = folder.MakeFile("graphite.json", "{}");

	ImGuiFrames frames;
	TempAppSettings settings;
	Core::EventBus events;
	Graphite::Workspace workspace{events, settings.Get()};
	Core::DocumentSet documents;
	REQUIRE(documents.Open(settingsFile, Core::TabKind::Permanent) == Core::OpenOutcome::Opened);

	Graphite::DiagnosticsModel diagnostics;
	diagnostics.Apply(OneErrorIn(settingsFile));

	const Graphite::ActiveScheme scheme;
	Graphite::ProblemsPanel panel{diagnostics, documents, workspace, scheme};

	float rowEndInset = 0.0f;

	// As the panel manager begins a flush panel's window: with no padding at its sides.
	frames.Next([&] {
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, ImGui::GetStyle().WindowPadding.y));
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(kPanelSize);
		ImGui::Begin("Problems", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings);
		ImGui::PopStyleVar();
		panel.OnImGuiRender();
		rowEndInset = Graphite::ListRowEndInset();
		ImGui::End();
	});

	const ImGuiWindow* list = FindProblemList();
	REQUIRE(list != nullptr);

	const std::optional<HorizontalExtent> badge =
		HorizontalExtentInColour(*list->DrawList, Graphite::ColourOf(Graphite::FileKind::Json, kDarkScheme));

	REQUIRE(badge.has_value());
	CHECK(badge->Left >= list->Pos.x + rowEndInset);
	CHECK(badge->Left < list->Pos.x + rowEndInset + Graphite::FileIconWidth());
}
