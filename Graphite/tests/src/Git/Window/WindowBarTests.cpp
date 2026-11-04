#include <doctest/doctest.h>

#include "GitFixtures.h"
#include "ImGuiFrames.h"
#include "Git/Window/WindowBar.h"
#include "Theme/BuiltInSchemes.h"

#include <imgui.h>

#include <filesystem>
#include <string>
#include <vector>

namespace {

	const std::filesystem::path kFolder = "C:/work/graphite/";

	Core::Branch HeadBranch(const std::string& name)
	{
		Core::Branch branch;
		branch.Name = name;
		branch.FullName = "refs/heads/" + name;
		branch.IsHead = true;
		return branch;
	}

	Core::Branch Tracking(Core::Branch branch, const std::string& upstream, std::size_t ahead, std::size_t behind)
	{
		branch.Upstream = upstream;
		branch.Ahead = ahead;
		branch.Behind = behind;
		branch.DistanceKnown = true;
		return branch;
	}

	Core::HeadState OnBranch(const std::string& name)
	{
		Core::HeadState head;
		head.Kind = Core::HeadKind::Branch;
		head.BranchName = name;
		head.Commit = OidWithFirstByte(1);
		return head;
	}

	Graphite::WindowBarText Describe(const Core::HeadState* head, const std::vector<Core::Branch>* branches,
									 const std::vector<Core::Remote>* remotes, bool checkingOut = false)
	{
		return Graphite::DescribeWindowBar(Graphite::WindowBarInput{kFolder, head, branches, remotes, checkingOut});
	}
}

TEST_CASE("Git window: the bar names the repository and its remotes")
{
	const Core::HeadState head = OnBranch("main");
	const std::vector<Core::Branch> branches = {HeadBranch("main")};
	const std::vector<Core::Remote> remotes = {{"origin", "https://example.com/a.git"}, {"backup", "ssh://host/a.git"}};

	const Graphite::WindowBarText text = Describe(&head, &branches, &remotes);

	CHECK(text.Repository == "graphite");
	CHECK(text.Remotes == "origin \xC2\xB7 backup");
	CHECK(text.RemotesTooltip == "origin: https://example.com/a.git\nbackup: ssh://host/a.git");
}

TEST_CASE("Git window: the bar says so when there are no remotes and holds the place while they are read")
{
	const Core::HeadState head = OnBranch("main");
	const std::vector<Core::Branch> branches = {HeadBranch("main")};
	const std::vector<Core::Remote> none;

	CHECK(Describe(&head, &branches, &none).Remotes == "no remotes");
	CHECK(Describe(&head, &branches, nullptr).Remotes == "\xE2\x80\xA6");
}

TEST_CASE("Git window: the head chip says what is to push and pull in words")
{
	const Core::HeadState head = OnBranch("main");
	const std::vector<Core::Remote> remotes;

	std::vector<Core::Branch> branches = {Tracking(HeadBranch("main"), "origin/main", 2, 0)};
	Graphite::WindowBarText text = Describe(&head, &branches, &remotes);
	CHECK(text.Head == "main");
	CHECK(text.HeadState == "2 to push");

	branches = {Tracking(HeadBranch("main"), "origin/main", 0, 3)};
	CHECK(Describe(&head, &branches, &remotes).HeadState == "3 to pull");

	branches = {Tracking(HeadBranch("main"), "origin/main", 2, 3)};
	CHECK(Describe(&head, &branches, &remotes).HeadState == "2 to push \xC2\xB7 3 to pull");

	branches = {Tracking(HeadBranch("main"), "origin/main", 0, 0)};
	CHECK(Describe(&head, &branches, &remotes).HeadState.empty());

	branches = {HeadBranch("main")};
	text = Describe(&head, &branches, &remotes);
	CHECK(text.HeadState.empty());
	CHECK(text.HeadTooltip == "main has no upstream branch");
}

TEST_CASE("Git window: a detached head is named by its commit")
{
	Core::HeadState head;
	head.Kind = Core::HeadKind::Detached;
	head.Commit = OidWithFirstByte(0xAB);
	const std::vector<Core::Branch> branches;
	const std::vector<Core::Remote> remotes;

	const Graphite::WindowBarText text = Describe(&head, &branches, &remotes);

	CHECK(text.Head == "detached at " + head.Commit->ShortHex());
	CHECK(text.HeadState.empty());
	CHECK(text.HeadTooltip == "HEAD is detached at " + head.Commit->ToHex());
}

TEST_CASE("Git window: an unborn head says it has no commits yet")
{
	Core::HeadState head;
	head.Kind = Core::HeadKind::Unborn;
	head.BranchName = "main";
	const std::vector<Core::Branch> branches;
	const std::vector<Core::Remote> remotes;

	CHECK(Describe(&head, &branches, &remotes).Head == "main \xC2\xB7 no commits yet");
}

TEST_CASE("Git window: a checkout under way marks the bar busy and an unread head names nothing")
{
	const Core::HeadState head = OnBranch("main");
	const std::vector<Core::Branch> branches = {HeadBranch("main")};
	const std::vector<Core::Remote> remotes;

	const Graphite::WindowBarText busy = Describe(&head, &branches, &remotes, true);
	CHECK(busy.Busy);
	CHECK(busy.Head.empty());

	const Graphite::WindowBarText unread = Describe(nullptr, &branches, &remotes);
	CHECK_FALSE(unread.Busy);
	CHECK(unread.Head.empty());
}

TEST_CASE("Git window: the bar is drawn one bar high and ends on an item")
{
	ImGuiFrames frames;
	const Graphite::ResolvedScheme scheme = Graphite::DarkScheme();
	const Core::HeadState head = OnBranch("main");
	const std::vector<Core::Branch> branches = {Tracking(HeadBranch("main"), "origin/main", 2, 0)};
	const std::vector<Core::Remote> remotes = {{"origin", "https://example.com/a.git"}};
	const Graphite::WindowBarText text = Describe(&head, &branches, &remotes);

	float height = 0.0f;
	float fontSize = 0.0f;

	for (int frame = 0; frame < 2; ++frame)
	{
		frames.Next([&] {
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(900.0f, 200.0f));
			ImGui::Begin("WindowBarHarness", nullptr,
						 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);

			const float top = ImGui::GetCursorPosY();
			const Graphite::WindowBarColours colours = Graphite::ReadWindowBarColours(scheme);
			Graphite::DrawWindowBar(Graphite::WindowBarDrawing{.Text = text, .WorkingDirectory = kFolder, .Colours = colours});
			height = ImGui::GetCursorPosY() - top - ImGui::GetStyle().ItemSpacing.y;
			fontSize = ImGui::GetFontSize();

			ImGui::End();
		});
	}

	CHECK(height == doctest::Approx(Graphite::WindowBarEm(36.0f)));
	CHECK(height > fontSize);
}
