#include <doctest/doctest.h>

#include "Git/Diff/DiffBody.h"

#include "DrawnGlyphs.h"
#include "GitFixtures.h"
#include "ImGuiFrames.h"

#include "Core/Git/Status.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstddef>
#include <string>

namespace {

	constexpr const char* kHostWindow = "Diff body host";
	constexpr float kNoTimePasses = 0.0f;

	Core::FileDiff ModifiedFile(const char* path)
	{
		Core::FileDiff file;
		file.Change = Core::FileChange::Modified;
		file.NewPath = path;
		file.OldPath = path;
		return file;
	}

	Graphite::DiffBody WorkingBody()
	{
		Graphite::DiffBody body;
		body.Diff = Graphite::OpenDiff{.Target = Core::DiffTarget::WorkTreeToIndex, .Path = "a.txt"};
		return body;
	}

	// The glyphs one frame of the body leaves in its window.
	std::size_t DrawnGlyphCount(ImGuiFrames& frames, Graphite::DiffBody& body, Graphite::GitModel& model)
	{
		const Graphite::GitPalette palette;
		const Graphite::SyntaxPalette syntax;
		const Graphite::DiffBodyStyle style{.Palette = palette, .Syntax = syntax};

		frames.Next([&] {
			ImGui::SetNextWindowSize(ImVec2(800.0f, 400.0f));
			ImGui::Begin(kHostWindow);
			Graphite::DrawDiffBody(body, model, style);
			ImGui::End();
		});

		const ImGuiWindow* window = ImGui::FindWindowByName(kHostWindow);
		REQUIRE(window != nullptr);
		return DrawnGlyphs(*window->DrawList).size();
	}
}

TEST_CASE("A working-tree diff body asks for that file's diff and draws it once it lands")
{
	ImGuiFrames frames;
	EmptyGitPort port;
	port.DiffAnswer.Files.push_back(ModifiedFile("a.txt"));
	Graphite::GitModel model(port, RunJobsInline());
	model.SetRoot("C:/repo");

	Graphite::DiffBody body = WorkingBody();

	DrawnGlyphCount(frames, body, model);
	model.OnUpdate(Core::Timestep{kNoTimePasses});
	DrawnGlyphCount(frames, body, model);

	CHECK(model.GetWorkingTreeDiff() != nullptr);
}

TEST_CASE("A diff body whose diff failed offers Retry")
{
	ImGuiFrames frames;
	EmptyGitPort port;
	port.DiffFailure = Core::Error{Core::ErrorCode::IoFailure, "index locked"};
	Graphite::GitModel model(port, RunJobsInline());
	model.SetRoot("C:/repo");

	Graphite::DiffBody body = WorkingBody();

	DrawnGlyphCount(frames, body, model);
	model.OnUpdate(Core::Timestep{kNoTimePasses});
	const std::size_t glyphs = DrawnGlyphCount(frames, body, model);

	const std::string message = *Graphite::DiffFailureOf(body.Diff, model);
	const std::size_t messageGlyphs =
		static_cast<std::size_t>(std::count_if(message.begin(), message.end(), [](char c) { return c != ' '; }));
	constexpr std::size_t kRetryGlyphs = 5;

	CHECK(glyphs >= messageGlyphs + kRetryGlyphs);
}
