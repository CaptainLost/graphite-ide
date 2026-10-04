#pragma once

#include "Core/ImGui/ToastStack.h"

#include <cstdint>
#include <string>

namespace Graphite {

	// What a toast is about, which decides how long it may stay up.
	enum class ToastScope
	{
		// Applies whatever folder is open - a settings file that failed to load, a copy.
		Application,

		// About the open folder or its repository: cleared when that folder closes or another
		// replaces it, since an error toast about it would otherwise outlive it.
		Folder
	};

	// A toast any feature raises by publishing this on the application's event bus, so no
	// feature holds the toasts themselves. Published and handled on the UI thread, like every
	// event on the bus.
	struct ToastEvent
	{
		Core::ToastKind Kind = Core::ToastKind::Error;

		// The action that failed or finished - "Checkout failed" - and why.
		std::string Title;
		std::string Message;

		// What the toast's Copy copies; empty for the title and the message, one line each.
		std::string CopyText;

		ToastScope Scope = ToastScope::Application;
	};

	// The tag a toast of `scope` is shown under, for Core::Toasts::Clear(group).
	constexpr std::uint32_t ToastGroupOf(ToastScope scope)
	{
		return scope == ToastScope::Folder ? 1u : 0u;
	}
}
