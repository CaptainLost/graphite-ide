#include "Notifications/NotificationCentre.h"

#include <cstdint>
#include <vector>

namespace Graphite {

	void NotificationCentre::Show(const ToastEvent& toast, const std::filesystem::path& folder)
	{
		const std::uint64_t toastId = m_Toasts.Show(toast.Kind, toast.Title, toast.Message, toast.CopyText, ToastGroupOf(toast.Scope));
		const bool aboutTheFolder = toast.Scope == ToastScope::Folder;

		m_History.Record({
			.ToastId = toastId,
			.Kind = toast.Kind,
			.Title = toast.Title,
			.Message = toast.Message,
			.CopyText = toast.CopyText,
			.Folder = aboutTheFolder ? folder : std::filesystem::path(),
			.LastShownAt = std::chrono::system_clock::now(),
		});
	}

	void NotificationCentre::ClearFolder()
	{
		m_Toasts.Clear(ToastGroupOf(ToastScope::Folder));
	}

	std::optional<Core::ToastKind> NotificationCentre::MissedForTheBell()
	{
		for (const Core::ToastDeparture& departure : m_Toasts.TakeDepartures())
		{
			m_History.NoteDeparture(departure);
		}

		if (m_Popup.IsOpen())
		{
			return std::nullopt;
		}

		return m_History.MostSevereMissed();
	}

	void NotificationCentre::OpenFromBell(std::string_view bellWindowName)
	{
		m_Popup.OpenFromBell(bellWindowName);
	}

	void NotificationCentre::Render(const Core::ToastPalette& palette, const std::filesystem::path& openFolder,
									const std::chrono::time_zone* zone)
	{
		m_Popup.Render(m_History, palette, openFolder, zone);
		m_Toasts.Render(palette);
	}
}
