#pragma once

#include "Core/Base/Result.h"
#include "Core/Git/Head.h"
#include "Core/Git/Ref.h"
#include "Core/Git/Remote.h"
#include "Git/Network/NetworkState.h"
#include "Theme/ColorScheme.h"

#include <imgui.h>

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	struct WindowBarInput
	{
		const std::filesystem::path& WorkingDirectory;
		const Core::HeadState* Head;
		const std::vector<Core::Branch>* Branches;
		const std::vector<Core::Remote>* Remotes;
		bool CheckingOut;

		// The message of a failed remotes read; null otherwise. Said instead of "no remotes", which only an answer may say.
		const std::string* RemotesFailure = nullptr;
	};

	struct WindowBarText
	{
		std::string Repository;
		std::string Remotes; // "…" while they are unread, so the place is held
		std::string RemotesTooltip;
		std::string Head; // empty while HEAD is unread or a checkout is under way
		std::string HeadState; // "2 to push", "3 to pull", both joined; drawn in the match colour
		std::string HeadTooltip;
		bool Busy = false; // a checkout is under way: the chip shows the loader, saying what Head holds

		// HEAD is on a branch, so a click on the chip also shows the branch in the sidebar. Not while HEAD is detached or
		// has no commits yet: then the click shows HEAD's history alone.
		bool OnBranch = false;
	};

	WindowBarText DescribeWindowBar(const WindowBarInput& input);

	// The chip's tooltip: HeadTooltip, and on a branch a second line saying what a click does.
	std::string HeadChipTooltip(const WindowBarText& text);

	// One of Fetch, Pull and Push as the bar draws it: "Pull" and its count apart, the count drawn in the match colour.
	struct ButtonText
	{
		// "Fetch", "Pull", "Push"; while this button's operation runs, "Fetching… 45%", "Pulling…", "Pushing…".
		std::string Label;

		// The distance to the upstream when it is over 0; empty otherwise and while the button runs.
		std::string Count;

		// What a hover says: the action, why the button is disabled, or "Click to cancel". Empty while nothing is known.
		std::string Tooltip;

		bool Enabled = false;

		// This button's operation runs: a click on it cancels, when Enabled.
		bool Running = false;

		// Push without an upstream on a remote, with several remotes: a click asks which remote, and Tooltip is the
		// question, "Push main to…".
		bool AsksWhichRemote = false;
	};

	struct NetworkButtons
	{
		ButtonText Fetch;
		ButtonText Pull;
		ButtonText Push;

		// "fetched 5m ago", "fetched just now"; empty in a repository never fetched.
		std::string FetchedAge;
	};

	// WARNING: The three keep their places and are disabled, with no count, until the branches and remotes are listed.
	NetworkButtons DescribeNetworkButtons(const WindowBarInput& input, const NetworkState& state,
										  std::optional<std::chrono::sys_seconds> lastFetch, std::chrono::sys_seconds now, bool gitFound);

	enum class NetworkClick
	{
		None,
		Fetch,
		Pull,
		Push,
		Cancel
	};

	struct WindowBarClick
	{
		// The HEAD chip was clicked.
		bool Head = false;
		NetworkClick Network = NetworkClick::None;
	};

	struct SettledWindowBar
	{
		WindowBarText Text;
		bool Complete = false; // Text is a state in which every value is known, not the waiting one
	};

	// The bar goes from waiting to complete in one step: until the repository's folder, HEAD (or its failure) and the
	// remotes (or their failure) are all known it holds `held`, the previous complete text of this repository, or the
	// waiting text when there is none.
	SettledWindowBar SettleWindowBar(const WindowBarInput& input, bool headFailed, const WindowBarText* held);

	// The failed reads the bar says at its end; null for one that did not fail. HEAD's is said in its chip's place, and a
	// commit's diff or texts beside the commit.
	struct WindowBarFailures
	{
		const Core::Error* Status = nullptr;
		const Core::Error* Branches = nullptr;
		const Core::Error* Log = nullptr;
		const Core::Error* Lines = nullptr; // a read of the branches' lines, or a change to them not saved
	};

	// Each failure's message, in that order, joined by " · "; empty when none failed.
	std::string WindowBarProblem(const WindowBarFailures& failures);

	// Every colour comes from the active scheme.
	struct WindowBarColours
	{
		ImU32 Dim = 0;
		ImU32 Icon = 0; // an enabled network button's icon: the mockup's .wbtn .ic, a little dimmer than the text
		ImU32 Match = 0;
		ImU32 Rule = 0;
		ImU32 ChipFill = 0;
		ImU32 ChipHoveredFill = 0;
		ImU32 Accent = 0;
		ImU32 OnAccent = 0;
		ImU32 AskFill = 0;
		ImU32 AskQuestion = 0;
		ImU32 Automatic = 0; // the ink of the picker's "auto" chip, the green of a base that holds a commit
		ImU32 Problem = 0;   // a failed read said at the bar's end
	};

	WindowBarColours ReadWindowBarColours(const ResolvedScheme& scheme);

	// A length of the mockup, in px at its 13 px font, scaled to the font in use.
	float WindowBarEm(float mockupPixels);

	// Where the bar put what it drew, in screen pixels; a part not drawn keeps an empty rectangle. For tests of the layout.
	struct Place
	{
		ImVec2 Min{};
		ImVec2 Max{};
	};

	struct WindowBarPlaces
	{
		Place Repository;
		Place Remotes;
		Place Chip;
		Place Problem;
		Place Fetch;
		Place Pull;
		Place Push;
		Place Age;
	};

	struct WindowBarDrawing
	{
		const WindowBarText& Text;
		const std::filesystem::path& WorkingDirectory;

		// Told in place of the chip when HEAD cannot be read; empty otherwise.
		std::string_view HeadFailure;

		const WindowBarColours& Colours;

		// How far the bar reaches past the cursor on the left, the right and the top, to meet the window's edges: the
		// window's padding. The cursor lands where it would have without it, so what follows keeps its padding.
		ImVec2 Bleed{};

		// A read of the repository that failed, said at the bar's right end, cut to the room left and whole in its tooltip;
		// empty otherwise. The bar's height holds it, so it never pushes the window's content down.
		std::string_view Problem;

		// Fetch, Pull and Push and the age of the last fetch, at the bar's right end before the problem; null draws none.
		const NetworkButtons* Network = nullptr;

		// Filled while drawing when not null.
		WindowBarPlaces* Places = nullptr;
	};

	// One bar high across the available width, ending on an item. Says what was clicked.
	WindowBarClick DrawWindowBar(const WindowBarDrawing& drawing);
}
