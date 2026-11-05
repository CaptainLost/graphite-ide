#pragma once

#include "Core/Git/Head.h"
#include "Core/Git/Ref.h"
#include "Core/Git/Remote.h"
#include "Theme/ColorScheme.h"

#include <imgui.h>

#include <filesystem>
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
	};

	WindowBarText DescribeWindowBar(const WindowBarInput& input);

	struct SettledWindowBar
	{
		WindowBarText Text;
		bool Complete = false; // Text is a state in which every value is known, not the waiting one
	};

	// The bar goes from waiting to complete in one step: until the repository's folder, HEAD (or its failure) and the
	// remotes (or their failure) are all known it holds `held`, the previous complete text of this repository, or the
	// waiting text when there is none.
	SettledWindowBar SettleWindowBar(const WindowBarInput& input, bool headFailed, const WindowBarText* held);

	// Every colour comes from the active scheme.
	struct WindowBarColours
	{
		ImU32 Dim = 0;
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
	};

	// One bar high across the available width, ending on an item. True when the HEAD chip was clicked.
	bool DrawWindowBar(const WindowBarDrawing& drawing);
}
