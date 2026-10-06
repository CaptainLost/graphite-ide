#pragma once

#include "Notifications/NotificationHistory.h"

#include "Core/ImGui/Toasts.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string_view>
#include <unordered_set>

namespace Graphite {

	class NotificationsPopup
	{
	public:
		// Only opens: while the popup holds focus ImGui blocks the bell, and the press on it
		// closes the popup before the bell could report a click.
		void OpenFromBell();

		// Every frame, open or not.
		void Render(NotificationHistory& history, const Core::ToastPalette& palette,
					const std::filesystem::path& openFolder, const std::chrono::time_zone& zone);

		bool IsExpanded(std::uint64_t toastId) const { return m_Expanded.contains(toastId); }

		// Left empty, Copy goes straight to ImGui's clipboard.
		std::function<void(std::string_view)> OnCopied;

	private:
		void DrawHeader(NotificationHistory& history);
		void DrawEntries(const NotificationHistory& history, const Core::ToastPalette& palette,
						 const std::filesystem::path& openFolder, const std::chrono::time_zone& zone);
		void ToggleExpanded(std::uint64_t toastId);
		void Copy(const NotificationEntry& entry);

		bool m_OpenRequested = false;
		bool m_ShowConfirmations = false;
		std::unordered_set<std::uint64_t> m_Expanded;
	};
}
