#include <doctest/doctest.h>

#include "GitFixtures.h"
#include "ImGuiFrames.h"
#include "Git/Window/WindowBar.h"
#include "Theme/BuiltInSchemes.h"

#include <imgui.h>
#include <imgui_internal.h>

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

TEST_CASE("Git window: a failed remotes read is not said to be no remotes")
{
	const Core::HeadState head = OnBranch("main");
	const std::vector<Core::Branch> branches = {HeadBranch("main")};
	const std::string failure = "cannot open the config";

	const Graphite::WindowBarText text = Graphite::DescribeWindowBar(
		Graphite::WindowBarInput{kFolder, &head, &branches, nullptr, false, &failure});

	CHECK(text.Remotes == "cannot read remotes");
	CHECK(text.RemotesTooltip == failure);
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

	branches = {Tracking(HeadBranch("main"), "origin/main", 1'234, 56'789)};
	CHECK(Describe(&head, &branches, &remotes).HeadState == "1,234 to push \xC2\xB7 56,789 to pull");

	branches = {Tracking(HeadBranch("main"), "origin/main", 0, 0)};
	CHECK(Describe(&head, &branches, &remotes).HeadState.empty());

	branches = {HeadBranch("main")};
	text = Describe(&head, &branches, &remotes);
	CHECK(text.HeadState.empty());
	CHECK(text.HeadTooltip == "main has no upstream branch");
}

TEST_CASE("Git window: the head chip counts the branch HEAD names and not the one the listing has checked out")
{
	// Just after a checkout: HEAD is read at once, the branches are still the listing made before it.
	const Core::HeadState head = OnBranch("fresh-start");
	const std::vector<Core::Remote> remotes;

	Core::Branch freshStart;
	freshStart.Name = "fresh-start";
	freshStart.FullName = "refs/heads/fresh-start";

	std::vector<Core::Branch> branches = {Tracking(HeadBranch("develop"), "origin/develop", 1, 0), freshStart};
	Graphite::WindowBarText text = Describe(&head, &branches, &remotes);
	CHECK(text.Head == "fresh-start");
	CHECK(text.HeadState.empty());
	CHECK(text.HeadTooltip == "fresh-start has no upstream branch");

	branches = {Tracking(HeadBranch("develop"), "origin/develop", 1, 0), Tracking(freshStart, "origin/fresh-start", 0, 2)};
	text = Describe(&head, &branches, &remotes);
	CHECK(text.HeadState == "2 to pull");
	CHECK(text.HeadTooltip == "fresh-start tracks origin/fresh-start");
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

TEST_CASE("Git window: a failed read is said at the bar's end without widening the window")
{
	ImGuiFrames frames;
	const Graphite::ResolvedScheme scheme = Graphite::DarkScheme();
	const Core::HeadState head = OnBranch("main");
	const std::vector<Core::Branch> branches = {HeadBranch("main")};
	const std::vector<Core::Remote> remotes = {{"origin", "https://example.com/a.git"}};
	const Graphite::WindowBarText text = Describe(&head, &branches, &remotes);
	const std::string problem = "cannot diff: object not found - no match for id " + std::string(400, '0');

	float height = 0.0f;
	float reached = 0.0f;
	float contentRight = 0.0f;

	for (int frame = 0; frame < 2; ++frame)
	{
		frames.Next([&] {
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(900.0f, 200.0f));
			ImGui::Begin("WindowBarProblemHarness", nullptr,
						 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);

			const float top = ImGui::GetCursorPosY();
			contentRight = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
			const Graphite::WindowBarColours colours = Graphite::ReadWindowBarColours(scheme);
			Graphite::DrawWindowBar(Graphite::WindowBarDrawing{
				.Text = text, .WorkingDirectory = kFolder, .Colours = colours, .Problem = problem});
			height = ImGui::GetCursorPosY() - top - ImGui::GetStyle().ItemSpacing.y;
			reached = ImGui::GetCurrentWindow()->DC.CursorMaxPos.x;

			ImGui::End();
		});
	}

	CHECK(height == doctest::Approx(Graphite::WindowBarEm(36.0f)));
	CHECK(reached <= contentRight + 0.5f);
}

TEST_CASE("Git window: the bar waits while HEAD is known and the remotes are not")
{
	const Core::HeadState head = OnBranch("main");
	const std::vector<Core::Branch> branches = {HeadBranch("main")};

	const Graphite::SettledWindowBar settled = Graphite::SettleWindowBar(
		Graphite::WindowBarInput{kFolder, &head, &branches, nullptr, false}, false, nullptr);

	CHECK_FALSE(settled.Complete);
	CHECK(settled.Text.Head.empty());
	CHECK(settled.Text.Remotes == "\xE2\x80\xA6");
	CHECK(settled.Text.Repository == "graphite");
}

TEST_CASE("Git window: the bar is complete once HEAD and the remotes are both known")
{
	const Core::HeadState head = OnBranch("main");
	const std::vector<Core::Branch> branches = {HeadBranch("main")};
	const std::vector<Core::Remote> remotes = {{"origin", "https://example.com/a.git"}};

	const Graphite::SettledWindowBar settled = Graphite::SettleWindowBar(
		Graphite::WindowBarInput{kFolder, &head, &branches, &remotes, false}, false, nullptr);

	CHECK(settled.Complete);
	CHECK(settled.Text.Head == "main");
	CHECK(settled.Text.Remotes == "origin");
}

TEST_CASE("Git window: the bar waits for the repository's folder as it waits for HEAD and the remotes")
{
	const Core::HeadState head = OnBranch("main");
	const std::vector<Core::Branch> branches = {HeadBranch("main")};
	const std::vector<Core::Remote> remotes = {{"origin", "https://example.com/a.git"}};
	const std::filesystem::path unread;

	const Graphite::SettledWindowBar settled = Graphite::SettleWindowBar(
		Graphite::WindowBarInput{unread, &head, &branches, &remotes, false}, false, nullptr);

	CHECK_FALSE(settled.Complete);
	CHECK(settled.Text.Head.empty());

	const Graphite::SettledWindowBar named = Graphite::SettleWindowBar(
		Graphite::WindowBarInput{kFolder, &head, &branches, &remotes, false}, false, nullptr);

	CHECK(named.Complete);
	CHECK(named.Text.Repository == "graphite");
	CHECK(named.Text.Head == "main");
}

TEST_CASE("Git window: a failed remotes read or a failed HEAD read counts as known")
{
	const Core::HeadState head = OnBranch("main");
	const std::vector<Core::Branch> branches = {HeadBranch("main")};
	const std::string failure = "cannot open the config";

	const Graphite::SettledWindowBar remotesFailed = Graphite::SettleWindowBar(
		Graphite::WindowBarInput{kFolder, &head, &branches, nullptr, false, &failure}, false, nullptr);
	CHECK(remotesFailed.Complete);
	CHECK(remotesFailed.Text.Remotes == "cannot read remotes");

	const std::vector<Core::Remote> none;
	const Graphite::SettledWindowBar headFailed = Graphite::SettleWindowBar(
		Graphite::WindowBarInput{kFolder, nullptr, &branches, &none, false}, true, nullptr);
	CHECK(headFailed.Complete);
	CHECK(headFailed.Text.Remotes == "no remotes");
}

TEST_CASE("Git window: a refresh with the remotes pending keeps the previous complete bar")
{
	const Core::HeadState head = OnBranch("main");
	const std::vector<Core::Branch> branches = {HeadBranch("main")};
	const std::vector<Core::Remote> remotes = {{"origin", "https://example.com/a.git"}};

	const Graphite::SettledWindowBar before = Graphite::SettleWindowBar(
		Graphite::WindowBarInput{kFolder, &head, &branches, &remotes, false}, false, nullptr);

	const Graphite::SettledWindowBar during = Graphite::SettleWindowBar(
		Graphite::WindowBarInput{kFolder, &head, &branches, nullptr, false}, false, &before.Text);

	CHECK(during.Complete);
	CHECK(during.Text.Head == "main");
	CHECK(during.Text.Remotes == "origin");

	const std::vector<Core::Remote> changed = {{"origin", "u"}, {"backup", "v"}};
	const Graphite::SettledWindowBar after = Graphite::SettleWindowBar(
		Graphite::WindowBarInput{kFolder, &head, &branches, &changed, false}, false, &during.Text);
	CHECK(after.Text.Remotes == "origin \xC2\xB7 backup");
}

TEST_CASE("Git window: the remotes landing first and the HEAD failure after it ends the waiting")
{
	const std::vector<Core::Branch> branches = {HeadBranch("main")};
	const std::vector<Core::Remote> none;
	const Graphite::WindowBarInput input{kFolder, nullptr, &branches, &none, false};

	const Graphite::SettledWindowBar waiting = Graphite::SettleWindowBar(input, false, nullptr);
	CHECK_FALSE(waiting.Complete);

	const Graphite::SettledWindowBar failed = Graphite::SettleWindowBar(input, true, nullptr);
	CHECK(failed.Complete);
	CHECK(failed.Text.Remotes == "no remotes");
}

TEST_CASE("Git window: the bar's end says a failed read of the lines whether or not the repository's reads failed too")
{
	const Core::Error log{.Message = "cannot walk the history"};
	const Core::Error lines{.Message = "The line answers file cannot be read, so the change was not saved."};

	CHECK(Graphite::WindowBarProblem(Graphite::WindowBarFailures{}).empty());
	CHECK(Graphite::WindowBarProblem(Graphite::WindowBarFailures{.Lines = &lines}) == lines.Message);
	CHECK(Graphite::WindowBarProblem(Graphite::WindowBarFailures{.Log = &log}) == log.Message);
	CHECK(Graphite::WindowBarProblem(Graphite::WindowBarFailures{.Log = &log, .Lines = &lines}) ==
		  "cannot walk the history \xC2\xB7 The line answers file cannot be read, so the change was not saved.");
}

TEST_CASE("Git window: the bar's end says the status then the branches then the log then the lines")
{
	const Core::Error status{.Message = "status failed"};
	const Core::Error branches{.Message = "branches failed"};
	const Core::Error log{.Message = "log failed"};
	const Core::Error lines{.Message = "lines failed"};

	CHECK(Graphite::WindowBarProblem(Graphite::WindowBarFailures{.Status = &status, .Branches = &branches, .Log = &log, .Lines = &lines}) ==
		  "status failed \xC2\xB7 branches failed \xC2\xB7 log failed \xC2\xB7 lines failed");
}
