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

	struct SettingsRow
	{
		// WARNING: The key must stay the same while the row is on screen, so a row whose title changes
		// while it is clicked, such as an install's progress, sets one.
		std::string Key;
		std::string Title;
		std::string Description;
		std::string Problem;

		bool DimmedTitle = false;

		std::vector<std::string> Keywords;

		float ControlWidth = 0.0f;
		bool WideControl = false;
		std::function<void()> DrawControl;

		std::function<void()> DrawBlock;

		// WARNING: ImGui skips a block scrolled out of sight, so nothing inside it could scroll the page to
		// itself. Setting this costs the block that saving, so set it only for as long as the scroll takes.
		bool DrawnEvenOutOfSight = false;

		std::function<std::vector<SettingsRow>(std::span<const std::string> words)> FindInside;

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

		// WARNING: Both run for every page each frame the panel is drawn, not only for the open one: a search
		// shows rows of pages that are not open, and a row can open a dialog.
		virtual void Update() {}
		virtual void RenderDialogs() {}

		virtual void OnEvent(Core::Event&) {}

		virtual void OnDeactivated() {}

		virtual void OnOpenedFromSearch(std::string_view) {}
	};
}
