#pragma once

#include "Core/Base/Color.h"
#include "Core/ImGui/ToastStack.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_set>

namespace Core {

	// Core does not pick the colours, as with Theme::Apply: the application hands them in.
	struct ToastPalette
	{
		// The card's background, and the colour of the mark drawn over its icon.
		Color Surface;
		Color Text;

		// The count after the title and the close button, a step quieter than the title. The
		// message sits most of the way from this towards Text; a quoted name in it is in Text.
		Color TextDim;

		Color Success;
		Color Warning;
		Color Error;
	};

	// Short messages in the bottom-right corner of the main viewport, over everything else.
	// Owned once, by whatever draws the frame; how the rest of the application reaches it is
	// the owner's business.
	class Toasts
	{
	public:
		// The title names the action - "Commit failed" - and the message says why. The copy
		// text is what Copy in the toast's menu copies instead of the title and the message,
		// for a message shortened for the screen; see Toast::CopyText. The group is the owner's
		// tag for Clear(group); see Toast::Group.
		void Show(ToastKind kind, std::string title, std::string message, std::string copyText = {},
				  std::uint32_t group = 0);

		// Once a frame, after every panel has drawn, so the toasts sit on top.
		void Render(const ToastPalette& palette);

		// Every toast goes at once, for when what they report no longer applies - the folder
		// they were about has closed or been replaced.
		void Clear() { m_Stack.Clear(); }

		// Only the toasts shown in `group`: the folder closed, but a toast about something
		// else - the application's settings, say - still applies.
		void Clear(std::uint32_t group) { m_Stack.Clear(group); }

		// What Copy in a toast's menu does with the toast's copy text - the title and the
		// message, one line each, unless Show was given one of its own. Left empty, it goes
		// straight to ImGui's clipboard; an owner that copies some other way sets it.
		std::function<void(std::string_view)> OnCopied;

		const ToastStack& GetStack() const { return m_Stack; }

	private:
		ToastStack m_Stack;

		// The ImGui window that held the keyboard before a click went into a toast, to be
		// handed back once the button is up; 0 for none. An ImGuiID, kept as its integer so
		// this header needs no ImGui.
		std::uint32_t m_FocusBeforeToasts = 0;

		// The toasts drawn last frame, by id. A toast missing here is new, and its tween is cut
		// to where it rests instead of sliding in from wherever an old tween left off.
		std::unordered_set<std::uint64_t> m_Drawn;
	};
}
