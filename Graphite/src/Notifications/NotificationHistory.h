#pragma once

#include "Core/ImGui/ToastStack.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	struct NotificationEntry
	{
		std::uint64_t ToastId = 0;
		Core::ToastKind Kind = Core::ToastKind::Error;
		std::string Title;
		std::string Message;
		std::string CopyText;

		// Empty for a toast about the application rather than a folder.
		std::filesystem::path Folder;

		int Count = 1;
		std::chrono::system_clock::time_point LastShownAt;
		bool Missed = false;
	};

	class NotificationHistory
	{
	public:
		static constexpr std::size_t kMostEntries = 200;

		void Record(NotificationEntry entry);
		void NoteDeparture(const Core::ToastDeparture& departure);
		void MarkAllSeen();
		void Remove(std::uint64_t toastId);
		void Clear();

		std::optional<Core::ToastKind> MostSevereMissed() const;

		// Valid until the history next changes.
		std::vector<const NotificationEntry*> Entries(bool includeConfirmations) const;

	private:
		std::vector<NotificationEntry> m_NewestFirst;
	};

	std::string CopiedText(const NotificationEntry& entry);
}
