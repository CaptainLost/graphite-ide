#pragma once

#include "Git/Compare/VsPicker.h"
#include "Git/Window/WindowBar.h"

#include "Core/Git/Oid.h"

#include <imgui.h>

#include <chrono>
#include <functional>
#include <optional>
#include <string>

namespace Graphite {

	// The popup under the history header's vs button: what a branch may be compared with, and the repository's bases.
	class VsPickerView
	{
	public:
		struct Drawing
		{
			const LineRead& Read;
			const BranchLines& Lines;
			std::chrono::sys_seconds Now;

			// How far the first tip is from the second; empty while asked.
			const std::function<std::optional<Core::LineDistance>(const Core::Oid& from, const Core::Oid& to)>& DistanceBetween;

			const WindowBarColours& Colours;

			// The screen rectangle the popup keeps inside.
			ImVec2 RegionMin;
			ImVec2 RegionMax;
		};

		// `below` is the screen point the popup's top left is put at.
		void Open(std::string branch, ImVec2 below);

		void Close() { m_Open = false; }

		bool IsOpen() const { return m_Open; }

		const std::string& Branch() const { return m_Branch; }

		// Draws the popup when it is open. Closes it when the branch is gone from the read, or by Esc or a click outside.
		// Returns the change the user chose; the popup is closed by then.
		std::optional<LineAnswerChange> Draw(const Drawing& drawing);

		static constexpr const char* kPopupId = "##vsPicker";

	private:
		void DrawAnotherBranch(const VsOption& option, float width, const WindowBarColours& colours);

		bool m_Open = false;
		bool m_PopupRequested = false;
		std::string m_Branch;
		std::string m_Search;

		// The "Another branch…" row is the search field while this holds.
		bool m_Searching = false;
		bool m_FocusSearch = false;
		bool m_SearchWasActive = false;
		ImVec2 m_Below{};
	};
}
