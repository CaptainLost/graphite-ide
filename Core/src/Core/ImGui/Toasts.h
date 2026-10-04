#pragma once

#include "Core/Base/Color.h"
#include "Core/ImGui/ToastStack.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace Core {

	// Core does not pick the colours, as with Theme::Apply: the application hands them in.
	struct ToastPalette
	{
		Color Surface;
		Color Text;
		Color Success;
		Color Warning;
		Color Error;
	};

	// Short messages in the bottom-right corner of the main viewport, over everything else.
	// One owner - the application layer - which hands it by reference to whatever reports.
	class Toasts
	{
	public:
		void Show(ToastKind kind, std::string message);

		// Once a frame, after every panel has drawn, so the toasts sit on top.
		void Render(const ToastPalette& palette);

		// Every toast goes at once, for when what they report no longer applies - the folder
		// they were about has closed or been replaced.
		void Clear() { m_Stack.Clear(); }

		// What Copy in a toast's menu does with the message. Left empty, it goes straight to
		// ImGui's clipboard; the application sets it to its own copy, which also reports it.
		std::function<void(std::string_view)> OnCopied;

		const ToastStack& GetStack() const { return m_Stack; }

	private:
		ToastStack m_Stack;

		// The ImGui window that held the keyboard before a click went into a toast, to be
		// handed back once the button is up; 0 for none. An ImGuiID, kept as its integer so
		// this header needs no ImGui.
		std::uint32_t m_FocusBeforeToasts = 0;
	};
}
