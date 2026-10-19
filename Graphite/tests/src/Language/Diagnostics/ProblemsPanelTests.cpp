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

TEST_CASE("A file in Problems draws its glyph in its kind's colour and its name in the text colour past it")
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
	const std::filesystem::path settingsFile = folder.MakeFile("graphite.json", "{}");
	const std::filesystem::path sourceFile = folder.MakeFile("main.cpp", "int main() {}");

	ImGuiFrames frames;
	TempAppSettings settings;
	Core::EventBus events;
	Graphite::Workspace workspace{events, settings.Get()};
	Core::DocumentSet documents;
	REQUIRE(documents.Open(settingsFile, Core::TabKind::Permanent) == Core::OpenOutcome::Opened);
	REQUIRE(documents.Open(sourceFile, Core::TabKind::Permanent) == Core::OpenOutcome::Opened);

	Graphite::DiagnosticsModel diagnostics;
	diagnostics.Apply(OneErrorIn(settingsFile));
	diagnostics.Apply(OneErrorIn(sourceFile));

	const Graphite::ActiveScheme scheme;
	Graphite::ProblemsPanel panel{diagnostics, documents, workspace, scheme};

	float pitch = 0.0f;

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

	const std::optional<VerticalExtent> settingsGlyph =
		VerticalExtentInColour(*list->DrawList, Graphite::ColourOf(Graphite::FileKind::Json, kDarkScheme));
	const std::optional<VerticalExtent> sourceGlyph =
		VerticalExtentInColour(*list->DrawList, Graphite::ColourOf(Graphite::FileKind::CppSource, kDarkScheme));

	REQUIRE(settingsGlyph.has_value());
	REQUIRE(sourceGlyph.has_value());

	constexpr float kHeadingAndOneProblem = 2.0f;
	CHECK(std::abs(sourceGlyph->Top - settingsGlyph->Top) == doctest::Approx(kHeadingAndOneProblem * pitch));
}
