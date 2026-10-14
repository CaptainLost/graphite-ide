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
		// WARNING: Only opens. While the popup holds focus ImGui blocks the bell, and the press on it closes the
		// popup before the bell could report a click. That press focuses the bell's window and ImGui hands
		// nothing back, so the popup gives the keyboard back from there.
		void OpenFromBell(std::string_view bellWindowName);

		void Render(NotificationHistory& history, const Core::ToastPalette& palette,
					const std::filesystem::path& openFolder, const std::chrono::time_zone* zone);

		bool IsExpanded(std::uint64_t toastId) const { return m_Expanded.contains(toastId); }
		bool IsOpen() const { return m_Open; }

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

		std::uint32_t m_KeyboardHolderOnOpening = 0;
	};
}
