#pragma once

#include "Notifications/NotificationHistory.h"
#include "Notifications/NotificationsPopup.h"
#include "UI/ToastEvent.h"

#include "Core/ImGui/Toasts.h"

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace Graphite {

	// Every notification the application raises: shown as a toast, recorded in the history the status bar's bell opens,
	// and counted as missed when its toast leaves unread.
	class NotificationCentre
	{
	public:
		// `folder` is the open folder a toast about the folder is recorded under; empty for one about the application.
		void Show(const ToastEvent& toast, const std::filesystem::path& folder);

		// The folder's toasts go, as it closes or another opens; the history keeps them.
		void ClearFolder();

		// Notes the toasts that left since it was last asked, then answers the most severe one missed while the list is
		// closed: what the bell's dot is coloured by, none while the list is open, since it marks all seen as it draws.
		// WARNING: Asked before Render, so a toast cleared with its folder since the last frame counts this frame.
		std::optional<Core::ToastKind> MissedForTheBell();

		// Only opens: see NotificationsPopup::OpenFromBell.
		void OpenFromBell(std::string_view bellWindowName);

		// The list when it is open, then the toasts over everything.
		void Render(const Core::ToastPalette& palette, const std::filesystem::path& openFolder, const std::chrono::time_zone* zone);

		const NotificationHistory& GetHistory() const { return m_History; }

	private:
		Core::Toasts m_Toasts;
		NotificationHistory m_History;
		NotificationsPopup m_Popup;
	};
}
