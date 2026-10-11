#pragma once

#include "Core/Base/NonCopyable.h"

#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Core {

	class Event;
}

namespace Graphite {

	class SettingsDirectory;

	// One line of a settings card: a title and a sentence about it on the left, a control on the
	// right. A block row has no title column and draws across the whole card; its title and
	// keywords are only for the search.
	struct SettingsRow
	{
		// Must stay the same while the row is on screen. A row whose title changes while it is
		// clicked, such as an install's progress, sets one.
		std::string Key;
		std::string Title;
		std::string Description;
		std::string Problem;
		std::vector<std::string> Keywords;

		float ControlWidth = 0.0f;
		bool WideControl = false;
		std::function<void()> DrawControl;

		std::function<void()> DrawBlock;

		// The rows a block shows in the search results for the query's lowered words, one per thing
		// inside it that matches every word; empty when nothing does.
		std::function<std::vector<SettingsRow>(std::span<const std::string> words)> FindInside;

		// What the [Open] of a found row with no control of its own hands the page's OnOpenedFromSearch.
		std::string OpenWith;

		std::string_view GetKey() const { return Key.empty() ? std::string_view{Title} : std::string_view{Key}; }

		bool IsBlock() const { return static_cast<bool>(DrawBlock); }
	};

	struct SettingsCard
	{
		std::string Heading;
		std::string Note;
		std::vector<SettingsRow> Rows;
	};

	class SettingsPage : Core::NonCopyable
	{
	public:
		virtual ~SettingsPage() = default;

		virtual std::string_view GetId() const = 0;

		virtual std::string_view GetTitle() const = 0;

		virtual const char* GetIcon() const = 0;

		virtual std::vector<SettingsCard> Describe() = 0;

		virtual SettingsDirectory* GetChildren() { return nullptr; }

		// Both run for every page each frame the panel is drawn, not only for the open one: a search
		// shows rows of pages that are not open, and a row can open a dialog.
		virtual void Update() {}
		virtual void RenderDialogs() {}

		virtual void OnEvent(Core::Event&) {}

		virtual void OnDeactivated() {}

		virtual void OnOpenedFromSearch(std::string_view) {}
	};
}
