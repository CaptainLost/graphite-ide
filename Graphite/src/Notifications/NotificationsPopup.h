#pragma once

#include "Notifications/NotificationHistory.h"

#include "Core/ImGui/Toasts.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_set>

namespace Graphite {

	class NotificationsPopup
	{
	public:
		// Only opens: while the popup holds focus ImGui blocks the bell, and the press on it
		// closes the popup before the bell could report a click. That press focuses the bell's
		// window and ImGui hands nothing back, so the popup gives the keyboard back from there.
		void OpenFromBell(std::string_view bellWindowName);

		// Every frame, open or not.
		void Render(NotificationHistory& history, const Core::ToastPalette& palette,
					const std::filesystem::path& openFolder, const std::chrono::time_zone* zone);

		bool IsExpanded(std::uint64_t toastId) const { return m_Expanded.contains(toastId); }
		bool IsOpen() const { return m_Open; }

		// Left empty, Copy goes straight to ImGui's clipboard.
		std::function<void(std::string_view)> OnCopied;

	private:
		bool DrawHeader();
		void ToggleExpanded(std::uint64_t toastId);
		void Copy(const std::string& copiedText);
		void NoteClosed();
		void HandKeyboardBackFromBellWindow() const;

		bool m_OpenRequested = false;
		bool m_Open = false;
		bool m_ShowConfirmations = false;
		bool m_EntryMenuOpenLastFrame = false;
		std::unordered_set<std::uint64_t> m_Expanded;
		std::string m_BellWindowName;

		// An ImGuiID, kept as its integer so this header needs no ImGui; 0 for none.
		std::uint32_t m_KeyboardHolderOnOpening = 0;
	};
}
