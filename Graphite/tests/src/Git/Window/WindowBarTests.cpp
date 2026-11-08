#include <doctest/doctest.h>

#include "GitFixtures.h"
#include "ImGuiFrames.h"
#include "Git/Window/WindowBar.h"
#include "Theme/BuiltInSchemes.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
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

TEST_CASE("Git window: a head chip on a branch offers to show the branch in the list")
{
	const Core::HeadState head = OnBranch("main");
	const std::vector<Core::Branch> branches = {Tracking(HeadBranch("main"), "origin/main", 2, 0)};
	const std::vector<Core::Remote> remotes;

	const Graphite::WindowBarText text = Describe(&head, &branches, &remotes);

	CHECK(text.OnBranch);
	CHECK(Graphite::HeadChipTooltip(text) == "main tracks origin/main\nClick to show it in the list");
}

TEST_CASE("Git window: a detached or unborn head's chip says nothing about the list")
{
	const std::vector<Core::Branch> branches;
	const std::vector<Core::Remote> remotes;

	Core::HeadState detached;
	detached.Kind = Core::HeadKind::Detached;
	detached.Commit = OidWithFirstByte(0xAB);
	const Graphite::WindowBarText detachedText = Describe(&detached, &branches, &remotes);

	CHECK_FALSE(detachedText.OnBranch);
	CHECK(Graphite::HeadChipTooltip(detachedText) == "HEAD is detached at " + detached.Commit->ToHex());

	Core::HeadState unborn;
	unborn.Kind = Core::HeadKind::Unborn;
	unborn.BranchName = "main";
	const Graphite::WindowBarText unbornText = Describe(&unborn, &branches, &remotes);

	CHECK_FALSE(unbornText.OnBranch);
	CHECK(Graphite::HeadChipTooltip(unbornText) == "No commits yet on main");
}

namespace {

	// The bar alone in a window, and whether any frame since the last ask said the chip was clicked.
	struct BarHarness
	{
		explicit BarHarness(const char* windowName, Graphite::WindowBarText text)
			: WindowName(windowName)
			, Text(std::move(text))
		{
		}

		void Draw()
		{
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(900.0f, 200.0f));
			ImGui::Begin(WindowName, nullptr,
						 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);

			BarTop = ImGui::GetCursorScreenPos().y;
			Clicked = Graphite::DrawWindowBar(Graphite::WindowBarDrawing{.Text = Text, .WorkingDirectory = kFolder, .Colours = Colours}).Head ||
					  Clicked;
			ChipId = ImGui::GetID("##headChip");

			ImGui::End();
		}

		// Moves the pointer along the bar's middle until the chip is under it; false when it never is.
		bool PointAtChip()
		{
			constexpr float kStep = 4.0f;
			const auto draw = [this] { Draw(); };

			Frames.Next(draw);
			const float middle = BarTop + Graphite::WindowBarEm(36.0f) * 0.5f;

			for (float x = 0.0f; x < 900.0f; x += kStep)
			{
				ImGui::GetIO().AddMousePosEvent(x, middle);
				Frames.Next(draw);

				if (ImGui::GetCurrentContext()->HoveredId == ChipId)
				{
					ChipPoint = ImVec2(x, middle);
					return true;
				}
			}

			return false;
		}

		bool DrawsHoveredFill() const
		{
			const ImGuiWindow* window = ImGui::FindWindowByName(WindowName);

			for (const ImDrawVert& vertex : window->DrawList->VtxBuffer)
			{
				if (vertex.col == Colours.ChipHoveredFill)
				{
					return true;
				}
			}

			return false;
		}

		bool TakeClick() { return std::exchange(Clicked, false); }

		ImGuiFrames Frames;
		const char* WindowName;
		Graphite::WindowBarText Text;
		Graphite::WindowBarColours Colours = Graphite::ReadWindowBarColours(Graphite::DarkScheme());
		float BarTop = 0.0f;
		ImGuiID ChipId = 0;
		ImVec2 ChipPoint;
		bool Clicked = false;
	};
}

TEST_CASE("Git window: a head chip shows a hover look and answers a click whether or not HEAD is on a branch")
{
	const std::vector<Core::Branch> branches = {HeadBranch("main")};
	const std::vector<Core::Remote> remotes;

	// One harness at a time: each holds an ImGui context of its own.
	{
		const Core::HeadState onBranch = OnBranch("main");
		BarHarness branch("BranchChipHarness", Describe(&onBranch, &branches, &remotes));

		REQUIRE(branch.PointAtChip());
		CHECK(branch.DrawsHoveredFill());
		CHECK_FALSE(branch.TakeClick());

		branch.Frames.Click(branch.ChipPoint, [&] { branch.Draw(); });
		CHECK(branch.TakeClick());
	}

	{
		Core::HeadState detachedHead;
		detachedHead.Kind = Core::HeadKind::Detached;
		detachedHead.Commit = OidWithFirstByte(0xAB);
		BarHarness detached("DetachedChipHarness", Describe(&detachedHead, &branches, &remotes));

		REQUIRE(detached.PointAtChip());
		CHECK(detached.DrawsHoveredFill());
		CHECK_FALSE(detached.TakeClick());

		detached.Frames.Click(detached.ChipPoint, [&] { detached.Draw(); });
		CHECK(detached.TakeClick());
	}
}

namespace {

	const std::chrono::sys_seconds kNow{std::chrono::seconds{1'800'000'000}};

	Graphite::NetworkButtons Buttons(const Core::HeadState* head, const std::vector<Core::Branch>* branches,
									 const std::vector<Core::Remote>* remotes, const Graphite::NetworkState& state = {},
									 std::optional<std::chrono::sys_seconds> lastFetch = std::nullopt, bool gitFound = true)
	{
		return Graphite::DescribeNetworkButtons(Graphite::WindowBarInput{kFolder, head, branches, remotes, false}, state, lastFetch, kNow,
												gitFound);
	}

	const std::vector<Core::Remote> kOrigin = {{"origin", "https://example.com/a.git"}};

	constexpr const char* kGitMissingSentence =
		"Fetch, Pull and Push need git, which was not found on PATH or where Git for Windows installs it.";

	Core::Branch RemoteMain()
	{
		Core::Branch upstream;
		upstream.Name = "origin/main";
		upstream.FullName = "refs/remotes/origin/main";
		upstream.Kind = Core::RefKind::Remote;
		return upstream;
	}
}

TEST_CASE("Window bar: the buttons keep their places and are disabled before the branches are listed")
{
	const Core::HeadState head = OnBranch("main");
	const std::vector<Core::Branch> branches = {Tracking(HeadBranch("main"), "origin/main", 2, 3)};

	const std::vector<Graphite::NetworkButtons> all = {Buttons(nullptr, nullptr, nullptr), Buttons(&head, nullptr, &kOrigin),
													   Buttons(&head, &branches, nullptr)};

	for (const Graphite::NetworkButtons& buttons : all)
	{
		CHECK(buttons.Fetch.Label == "Fetch");
		CHECK(buttons.Pull.Label == "Pull");
		CHECK(buttons.Push.Label == "Push");

		for (const Graphite::ButtonText* button : {&buttons.Fetch, &buttons.Pull, &buttons.Push})
		{
			CHECK_FALSE(button->Enabled);
			CHECK_FALSE(button->Running);
			CHECK(button->Count.empty());
		}
	}
}

TEST_CASE("Window bar: each button says why it is disabled")
{
	const std::vector<Core::Remote> none;
	const Core::HeadState head = OnBranch("main");
	const std::vector<Core::Branch> noUpstream = {HeadBranch("main")};

	// Fetch and Push: no remote.
	Graphite::NetworkButtons buttons = Buttons(&head, &noUpstream, &none);
	CHECK_FALSE(buttons.Fetch.Enabled);
	CHECK(buttons.Fetch.Tooltip == "This repository has no remote.");
	CHECK_FALSE(buttons.Push.Enabled);
	CHECK(buttons.Push.Tooltip == "This repository has no remote.");

	// Pull: no upstream.
	buttons = Buttons(&head, &noUpstream, &kOrigin);
	CHECK(buttons.Fetch.Enabled);
	CHECK_FALSE(buttons.Pull.Enabled);
	CHECK(buttons.Pull.Tooltip == "main has no upstream branch.");
	CHECK(buttons.Push.Enabled);

	// A detached HEAD.
	Core::HeadState detached;
	detached.Kind = Core::HeadKind::Detached;
	detached.Commit = OidWithFirstByte(0xAB);
	buttons = Buttons(&detached, &noUpstream, &kOrigin);
	CHECK(buttons.Fetch.Enabled);
	CHECK_FALSE(buttons.Pull.Enabled);
	CHECK(buttons.Pull.Tooltip == "No branch is checked out.");
	CHECK_FALSE(buttons.Push.Enabled);
	CHECK(buttons.Push.Tooltip == "No branch is checked out.");

	// A local branch as the upstream is an upstream: Pull is enabled.
	Core::Branch develop;
	develop.Name = "develop";
	develop.FullName = "refs/heads/develop";
	const std::vector<Core::Branch> onLocal = {Tracking(HeadBranch("main"), "develop", 0, 1), develop};
	buttons = Buttons(&head, &onLocal, &kOrigin);
	CHECK(buttons.Pull.Enabled);
	CHECK(buttons.Pull.Count == "1");

	// Git missing before the listing: the buttons wait like any, with nothing said yet.
	buttons = Buttons(nullptr, nullptr, nullptr, {}, std::nullopt, false);
	CHECK_FALSE(buttons.Fetch.Enabled);
	CHECK(buttons.Fetch.Tooltip.empty());

	// An unborn HEAD.
	Core::HeadState unborn;
	unborn.Kind = Core::HeadKind::Unborn;
	unborn.BranchName = "main";
	buttons = Buttons(&unborn, &noUpstream, &kOrigin);
	CHECK_FALSE(buttons.Pull.Enabled);
	CHECK(buttons.Pull.Tooltip == "main has no upstream branch.");
	CHECK_FALSE(buttons.Push.Enabled);
	CHECK(buttons.Push.Tooltip == "main has no commits yet.");
}

TEST_CASE("Window bar: pull and push show the upstream distance")
{
	const Core::HeadState head = OnBranch("main");

	std::vector<Core::Branch> branches = {Tracking(HeadBranch("main"), "origin/main", 2, 1'234), RemoteMain()};
	Graphite::NetworkButtons buttons = Buttons(&head, &branches, &kOrigin);

	CHECK(buttons.Pull.Label == "Pull");
	CHECK(buttons.Pull.Count == "999+");
	CHECK(buttons.Pull.Tooltip == "Pull origin/main into main");
	CHECK(buttons.Push.Count == "2");
	CHECK(buttons.Push.Tooltip == "Push 2 commits to origin");
	CHECK_FALSE(buttons.Push.AsksWhichRemote);

	// Over 999 the slot reads 999+, the tooltip keeps the number.
	branches = {Tracking(HeadBranch("main"), "origin/main", 12'345, 999), RemoteMain()};
	buttons = Buttons(&head, &branches, &kOrigin);
	CHECK(buttons.Pull.Count == "999");
	CHECK(buttons.Push.Count == "999+");
	CHECK(buttons.Push.Tooltip == "Push 12,345 commits to origin");

	branches = {Tracking(HeadBranch("main"), "origin/main", 1, 0), RemoteMain()};
	buttons = Buttons(&head, &branches, &kOrigin);
	CHECK(buttons.Pull.Count.empty());
	CHECK(buttons.Push.Tooltip == "Push 1 commit to origin");

	branches = {Tracking(HeadBranch("main"), "origin/main", 0, 0), RemoteMain()};
	buttons = Buttons(&head, &branches, &kOrigin);
	CHECK(buttons.Push.Count.empty());
	CHECK(buttons.Push.Enabled);

	// A distance that could not be measured says no count.
	branches = {Tracking(HeadBranch("main"), "origin/main", 5, 5), RemoteMain()};
	branches.front().DistanceKnown = false;
	buttons = Buttons(&head, &branches, &kOrigin);
	CHECK(buttons.Pull.Count.empty());
	CHECK(buttons.Push.Count.empty());
}

TEST_CASE("Window bar: push asks which remote only with several remotes and no upstream on one")
{
	const Core::HeadState head = OnBranch("main");
	const std::vector<Core::Remote> two = {{"origin", "u"}, {"backup", "v"}};
	std::vector<Core::Branch> branches = {HeadBranch("main")};

	Graphite::NetworkButtons buttons = Buttons(&head, &branches, &two);
	CHECK(buttons.Push.AsksWhichRemote);
	CHECK(buttons.Push.Tooltip == "Push main to\xE2\x80\xA6");

	// One remote: pushed there at once.
	buttons = Buttons(&head, &branches, &kOrigin);
	CHECK_FALSE(buttons.Push.AsksWhichRemote);
	CHECK(buttons.Push.Tooltip == "Push main to origin");

	// An upstream on a remote answers the question.
	branches = {Tracking(HeadBranch("main"), "origin/main", 0, 0), RemoteMain()};
	CHECK_FALSE(Buttons(&head, &branches, &two).Push.AsksWhichRemote);

	// An upstream that is a local branch does not.
	Core::Branch local;
	local.Name = "develop";
	local.FullName = "refs/heads/develop";
	branches = {Tracking(HeadBranch("main"), "develop", 0, 0), local};
	CHECK(Buttons(&head, &branches, &two).Push.AsksWhichRemote);
}

TEST_CASE("Window bar: a running operation's button reads its progress and cancels on click, and the others are disabled")
{
	const Core::HeadState head = OnBranch("main");
	const std::vector<Core::Branch> branches = {Tracking(HeadBranch("main"), "origin/main", 2, 3), RemoteMain()};

	Graphite::NetworkState state;
	state.Running = Graphite::NetworkOperation::Fetch;
	state.Serial = 7;
	state.Percent = 45;
	state.Cancellable = true;

	Graphite::NetworkButtons buttons = Buttons(&head, &branches, &kOrigin, state);
	// The label never changes: the slot shows the percent, the tooltip says the rest.
	CHECK(buttons.Fetch.Label == "Fetch");
	CHECK(buttons.Fetch.Count == "45%");
	CHECK(buttons.Fetch.Running);
	CHECK(buttons.Fetch.Enabled);
	CHECK(buttons.Fetch.Tooltip == "Fetching\xE2\x80\xA6 45%\nClick to cancel");

	for (const Graphite::ButtonText* other : {&buttons.Pull, &buttons.Push})
	{
		CHECK_FALSE(other->Running);
		CHECK_FALSE(other->Enabled);
		CHECK(other->Count.empty());
	}

	state.Running = Graphite::NetworkOperation::Pull;
	state.Percent.reset();
	buttons = Buttons(&head, &branches, &kOrigin, state);
	CHECK(buttons.Pull.Label == "Pull");
	CHECK(buttons.Pull.Count.empty());
	CHECK(buttons.Pull.Tooltip == "Pulling\xE2\x80\xA6\nClick to cancel");
	CHECK(buttons.Pull.Enabled);

	// Pull's fast-forward cannot be stopped: the button says so by being disabled.
	state.Cancellable = false;
	buttons = Buttons(&head, &branches, &kOrigin, state);
	CHECK(buttons.Pull.Running);
	CHECK_FALSE(buttons.Pull.Enabled);
	CHECK(buttons.Pull.Tooltip == "Pulling\xE2\x80\xA6 the last step cannot be cancelled");

	state.Running = Graphite::NetworkOperation::Push;
	state.Cancellable = true;
	buttons = Buttons(&head, &branches, &kOrigin, state);
	CHECK(buttons.Push.Label == "Push");
	CHECK(buttons.Push.Tooltip == "Pushing\xE2\x80\xA6\nClick to cancel");

	// The full history's fetch has no button of its own: all three wait.
	state.Running = Graphite::NetworkOperation::FullHistory;
	buttons = Buttons(&head, &branches, &kOrigin, state);

	for (const Graphite::ButtonText* button : {&buttons.Fetch, &buttons.Pull, &buttons.Push})
	{
		CHECK_FALSE(button->Running);
		CHECK_FALSE(button->Enabled);
	}
}

TEST_CASE("Window bar: fetched age reads FETCH_HEAD's time")
{
	const Core::HeadState head = OnBranch("main");
	const std::vector<Core::Branch> branches = {HeadBranch("main")};

	CHECK(Buttons(&head, &branches, &kOrigin).FetchedAge.empty());
	CHECK(Buttons(&head, &branches, &kOrigin, {}, kNow - std::chrono::seconds{5}).FetchedAge == "fetched just now");
	CHECK(Buttons(&head, &branches, &kOrigin, {}, kNow - std::chrono::minutes{5}).FetchedAge == "fetched 5m ago");
	CHECK(Buttons(&head, &branches, &kOrigin, {}, kNow - std::chrono::hours{3}).FetchedAge == "fetched 3h ago");
	CHECK(Buttons(&head, &branches, &kOrigin, {}, kNow + std::chrono::minutes{5}).FetchedAge == "fetched just now");
}

TEST_CASE("Window bar: without git every button says git was not found")
{
	const Core::HeadState head = OnBranch("main");
	const std::vector<Core::Branch> branches = {Tracking(HeadBranch("main"), "origin/main", 1, 1)};
	const Graphite::NetworkButtons buttons = Buttons(&head, &branches, &kOrigin, {}, std::nullopt, false);

	for (const Graphite::ButtonText* button : {&buttons.Fetch, &buttons.Pull, &buttons.Push})
	{
		CHECK_FALSE(button->Enabled);
		CHECK(button->Tooltip == kGitMissingSentence);
		CHECK(button->Count.empty());
	}
}

namespace {

	// What the bar is drawn for: its window's width, the repository's folder, the remotes, the branch's distance and the
	// problem said at its end.
	struct BarCase
	{
		float Width = 900.0f;
		std::filesystem::path Folder = kFolder;
		std::vector<Core::Remote> Remotes = {{"origin", "https://example.com/a.git"}, {"backup", "ssh://host/a.git"}};
		std::size_t Ahead = 2;
		const char* Problem = "the status could not be read";
		float* WindowRight = nullptr;

		// The frames to draw in; a sweep of widths draws all of them in one.
		ImGuiFrames* Frames = nullptr;
	};

	// The bar drawn as `bar` says; says where each part stood.
	Graphite::WindowBarPlaces PlacesFor(const Graphite::NetworkButtons& network, const BarCase& bar)
	{
		ImGuiFrames ownFrames;
		ImGuiFrames& frames = bar.Frames != nullptr ? *bar.Frames : ownFrames;
		const Graphite::ResolvedScheme scheme = Graphite::DarkScheme();
		const Core::HeadState head = OnBranch("main");
		const std::vector<Core::Branch> branches = {Tracking(HeadBranch("main"), "origin/main", bar.Ahead, 0)};
		const Graphite::WindowBarText text =
			Graphite::DescribeWindowBar(Graphite::WindowBarInput{bar.Folder, &head, &branches, &bar.Remotes, false});
		Graphite::WindowBarPlaces places;
		const float width = bar.Width;
		float* const windowRight = bar.WindowRight;

		for (int frame = 0; frame < 2; ++frame)
		{
			frames.Next([&] {
				ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
				ImGui::SetNextWindowSize(ImVec2(width, 200.0f));
				ImGui::Begin("WindowBarPlacesHarness", nullptr,
							 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);

				if (windowRight != nullptr)
				{
					*windowRight = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
				}

				const Graphite::WindowBarColours colours = Graphite::ReadWindowBarColours(scheme);
				places = {};
				Graphite::DrawWindowBar(Graphite::WindowBarDrawing{.Text = text,
																   .WorkingDirectory = bar.Folder,
																   .Colours = colours,
																   .Problem = bar.Problem,
																   .Network = &network,
																   .Places = &places});
				ImGui::End();
			});
		}

		return places;
	}

	Graphite::WindowBarPlaces PlacesOf(const Graphite::NetworkButtons& network, float width = 900.0f, float* windowRight = nullptr)
	{
		BarCase bar;
		bar.Width = width;
		bar.WindowRight = windowRight;
		return PlacesFor(network, bar);
	}

	float WidthOfPlace(const Graphite::Place& place)
	{
		return place.Max.x - place.Min.x;
	}

	bool Drawn(const Graphite::Place& place)
	{
		return place.Max.x > place.Min.x;
	}

	bool SamePlace(const Graphite::Place& first, const Graphite::Place& second)
	{
		return first.Min.x == doctest::Approx(second.Min.x) && first.Min.y == doctest::Approx(second.Min.y) &&
			   first.Max.x == doctest::Approx(second.Max.x) && first.Max.y == doctest::Approx(second.Max.y);
	}

	Graphite::NetworkButtons ButtonsWith(std::size_t ahead, const Graphite::NetworkState& state = {})
	{
		const Core::HeadState head = OnBranch("main");
		const std::vector<Core::Branch> branches = {Tracking(HeadBranch("main"), "origin/main", ahead, 0), RemoteMain()};
		return Buttons(&head, &branches, &kOrigin, state, kNow - std::chrono::minutes{5});
	}

	void CheckEveryPlaceHolds(const Graphite::WindowBarPlaces& before, const Graphite::WindowBarPlaces& after)
	{
		CHECK(SamePlace(before.Repository, after.Repository));
		CHECK(SamePlace(before.Remotes, after.Remotes));
		CHECK(SamePlace(before.Chip, after.Chip));
		CHECK(SamePlace(before.Problem, after.Problem));
		CHECK(SamePlace(before.Fetch, after.Fetch));
		CHECK(SamePlace(before.Pull, after.Pull));
		CHECK(SamePlace(before.Push, after.Push));
		CHECK(SamePlace(before.Age, after.Age));
	}
}

TEST_CASE("Window bar: a count appearing or growing leaves every other part where it was")
{
	const Graphite::WindowBarPlaces none = PlacesOf(ButtonsWith(0));
	REQUIRE(none.Fetch.Max.x > none.Fetch.Min.x);
	REQUIRE(none.Age.Max.x > none.Age.Min.x);
	REQUIRE(none.Problem.Max.x > none.Problem.Min.x);
	REQUIRE(none.Chip.Max.x > none.Chip.Min.x);

	CheckEveryPlaceHolds(none, PlacesOf(ButtonsWith(2)));
	CheckEveryPlaceHolds(none, PlacesOf(ButtonsWith(136)));
	CheckEveryPlaceHolds(none, PlacesOf(ButtonsWith(12'345)));
}

TEST_CASE("Window bar: a running percent going from empty to 5 to 100 leaves every part where it was")
{
	const Graphite::WindowBarPlaces idle = PlacesOf(ButtonsWith(2));

	Graphite::NetworkState state;
	state.Running = Graphite::NetworkOperation::Fetch;
	state.Serial = 3;
	state.Cancellable = true;

	CheckEveryPlaceHolds(idle, PlacesOf(ButtonsWith(2, state)));
	state.Percent = 5;
	CheckEveryPlaceHolds(idle, PlacesOf(ButtonsWith(2, state)));
	state.Percent = 100;
	CheckEveryPlaceHolds(idle, PlacesOf(ButtonsWith(2, state)));

	state.Running = Graphite::NetworkOperation::Push;
	CheckEveryPlaceHolds(idle, PlacesOf(ButtonsWith(2, state)));
}

TEST_CASE("Window bar: a narrow bar keeps the network part's place and the remotes give way before the repository")
{
	float wideRight = 0.0f;
	float narrowRight = 0.0f;
	float tightRight = 0.0f;
	const Graphite::WindowBarPlaces wide = PlacesOf(ButtonsWith(2), 900.0f, &wideRight);
	const Graphite::WindowBarPlaces narrow = PlacesOf(ButtonsWith(2), 600.0f, &narrowRight);
	const Graphite::WindowBarPlaces tight = PlacesOf(ButtonsWith(2), 300.0f, &tightRight);

	REQUIRE(wide.Remotes.Max.x > wide.Remotes.Min.x);

	CHECK(narrowRight - narrow.Push.Max.x == doctest::Approx(wideRight - wide.Push.Max.x));
	CHECK(tightRight - tight.Push.Max.x == doctest::Approx(wideRight - wide.Push.Max.x));
	CHECK(narrow.Push.Min.x - narrow.Fetch.Min.x == doctest::Approx(wide.Push.Min.x - wide.Fetch.Min.x));

	// The remotes go first while the repository's name still stands.
	CHECK(narrow.Remotes.Max.x - narrow.Remotes.Min.x == doctest::Approx(0.0f));
	CHECK(narrow.Repository.Max.x > narrow.Repository.Min.x);

	// At 600 the repository and the chip are drawn and stop short of the buttons; at 300 there is room for neither.
	REQUIRE(Drawn(narrow.Repository));
	REQUIRE(Drawn(narrow.Chip));
	CHECK(narrow.Repository.Max.x <= narrow.Fetch.Min.x);
	CHECK(narrow.Chip.Max.x <= narrow.Fetch.Min.x);
	CHECK_FALSE(Drawn(tight.Repository));
	CHECK_FALSE(Drawn(tight.Remotes));
	CHECK_FALSE(Drawn(tight.Chip));
}

namespace {

	// Frames wide enough for the whole bar at its natural width.
	struct WideFrames
	{
		WideFrames() { ImGui::GetIO().DisplaySize = ImVec2(3000.0f, 720.0f); }

		ImGuiFrames Frames;
	};

	BarCase LongNamesWith(ImGuiFrames& frames, std::size_t ahead, float width)
	{
		BarCase bar;
		bar.Frames = &frames;
		bar.Width = width;
		bar.Ahead = ahead;
		bar.Folder = "C:/work/a-rather-long-repository-name-for-the-bar/";
		bar.Remotes = {{"origin-with-a-rather-long-name", "u"}, {"backup-with-another-long-name", "v"}};
		return bar;
	}

	// What the bar draws at one width, in the terms of the order it gives space up in.
	struct Shares
	{
		float Remotes = 0.0f;
		float Repository = 0.0f;
		float Chip = 0.0f;
	};

	Shares SharesAt(ImGuiFrames& frames, std::size_t ahead, float width)
	{
		const Graphite::WindowBarPlaces places = PlacesFor(ButtonsWith(0), LongNamesWith(frames, ahead, width));
		return Shares{WidthOfPlace(places.Remotes), WidthOfPlace(places.Repository), WidthOfPlace(places.Chip)};
	}
}

TEST_CASE("Window bar: as the room shrinks the remotes go first, then the repository, then the chip's words, then its name")
{
	constexpr float kPixelOfRounding = 2.0f;
	WideFrames wide;

	for (const std::size_t ahead : {std::size_t{0}, std::size_t{2}, std::size_t{136}})
	{
		const Shares whole = SharesAt(wide.Frames, ahead, 2400.0f);
		REQUIRE(whole.Remotes > 0.0f);
		REQUIRE(whole.Repository > 0.0f);
		REQUIRE(whole.Chip > 0.0f);

		// The chip's width without its words: the same branch with nothing to push.
		const float chipWithoutWords = SharesAt(wide.Frames, 0, 2400.0f).Chip;
		const float repositoryMinimum = Graphite::WindowBarEm(90.0f);

		bool sawRemotesCut = false;
		bool sawRepositoryCut = false;
		bool sawWordsDropped = false;
		bool sawNameCut = false;

		for (float width = 2400.0f; width > 330.0f; width -= 7.0f)
		{
			INFO("ahead " << ahead << " width " << width);
			const Shares at = SharesAt(wide.Frames, ahead, width);

			// Each step starts only when the one before it is spent.
			if (at.Remotes < whole.Remotes - kPixelOfRounding)
			{
				sawRemotesCut = true;

				// While some of the remotes' text still shows, nothing else has lost anything.
				if (at.Remotes > 0.0f)
				{
					CHECK(at.Repository >= whole.Repository - kPixelOfRounding);
					CHECK(at.Chip >= whole.Chip - kPixelOfRounding);
				}
			}

			if (at.Repository < whole.Repository - kPixelOfRounding)
			{
				sawRepositoryCut = true;
				CHECK(at.Remotes == doctest::Approx(0.0f));

				// The chip loses nothing until the repository is down to its minimum.
				if (at.Repository > repositoryMinimum + kPixelOfRounding && at.Chip > 0.0f)
				{
					CHECK(at.Chip >= whole.Chip - kPixelOfRounding);
				}
			}

			// A chip with no room for its chrome is left out, and the repository takes the room it freed.
			if (at.Chip < whole.Chip - kPixelOfRounding && at.Chip > 0.0f)
			{
				CHECK(at.Remotes == doctest::Approx(0.0f));
				CHECK(at.Repository <= repositoryMinimum + kPixelOfRounding);
			}

			if (ahead > 0 && at.Chip < whole.Chip - kPixelOfRounding && at.Chip >= chipWithoutWords - kPixelOfRounding)
			{
				sawWordsDropped = true;
			}

			if (at.Chip > 0.0f && at.Chip < chipWithoutWords - kPixelOfRounding)
			{
				sawNameCut = true;
				CHECK(at.Repository <= repositoryMinimum + kPixelOfRounding);
			}
		}

		CHECK(sawRemotesCut);
		CHECK(sawRepositoryCut);
		CHECK(sawNameCut);

		// Without words to push there is nothing to drop before the name is cut.
		CHECK(sawWordsDropped == (ahead > 0));
	}
}

TEST_CASE("Window bar: the problem starts after whatever was drawn before it, with or without the chip")
{
	WideFrames wide;

	for (const std::size_t ahead : {std::size_t{0}, std::size_t{136}})
	{
		for (float width = 2400.0f; width > 330.0f; width -= 7.0f)
		{
			BarCase bar = LongNamesWith(wide.Frames, ahead, width);
			bar.Problem = "!";
			const Graphite::WindowBarPlaces places = PlacesFor(ButtonsWith(0), bar);

			if (!Drawn(places.Problem))
			{
				continue;
			}

			INFO("ahead " << ahead << " width " << width);

			for (const Graphite::Place* before : {&places.Repository, &places.Remotes, &places.Chip})
			{
				CHECK((!Drawn(*before) || places.Problem.Min.x >= before->Max.x));
			}

			CHECK(places.Problem.Max.x <= places.Fetch.Min.x);
		}
	}
}
