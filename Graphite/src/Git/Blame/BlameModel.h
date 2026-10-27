#pragma once

#include "Core/Base/Base.h"

#include "Git/Blame/HeadBlame.h"
#include "Git/Blame/LineAuthors.h"
#include "Git/DeliveryInbox.h"
#include "Git/Debouncer.h"
#include "Git/GitModel.h"

#include "Core/Base/NonCopyable.h"
#include "Core/Base/Result.h"
#include "Core/Base/Timestep.h"
#include "Core/Events/EventBus.h"
#include "Core/Git/LineHistory.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>

namespace Core {

	class TextBuffer;
}

namespace Graphite {

	struct LineHistoryState
	{
		Core::Ref<const Core::LineHistory> History;
		std::optional<Core::Error> Error;
		bool InFlight = false;
	};

	// WARNING: Every job captures `this`, so the queue behind the submitter must be destroyed, and
	// its workers joined, before this object is. An owner holding both declares this BEFORE the
	// queue.
	class BlameModel : Core::NonCopyable
	{
	public:
		BlameModel(BlamePort& port, JobSubmitter submit);

		void SubscribeTo(Core::EventBus& events);

		void OnUpdate(Core::Timestep timestep);

		// WARNING: A hidden document is not passed to Update, so its buffer is neither copied nor
		// diffed until it is shown again. A repository change re-reads the head blame only of the
		// documents passed here since the OnUpdate before the one it settles in, which is the
		// documents on screen; the others are marked stale and read again on their next Update.
		void Update(const std::filesystem::path& path, const Core::TextBuffer& buffer);

		// Null until a table has arrived. A file with no committed history to lay over it (untracked, unusable, or a
		// head that could not be read) gets an empty table whose head says so, which a reader tells apart from a
		// table still on its way.
		Core::Ref<const LineAuthors> Get(const std::filesystem::path& path) const;

		// Ignored when the same request is already held or in flight for the path. A failed answer is
		// held too; ForgetLineHistory is how a caller retries.
		void RequestLineHistory(const std::filesystem::path& path, const Core::LineHistoryRequest& request);

		LineHistoryState GetLineHistory(const std::filesystem::path& path) const;

		void ForgetLineHistory(const std::filesystem::path& path);

		void Retain(const std::function<bool(const std::filesystem::path&)>& isOpen);

		void Clear();

		void Invalidate();

		static constexpr float kEditQuietSeconds = 0.1f;

	private:
		struct Entry
		{
			std::uint64_t HeadRequested = 0;
			Core::Ref<const HeadBlame> Head;

			// At most one head read is in flight a path. A request made while one is sets HeadAgain,
			// and the read is made again once the first lands.
			bool HeadInFlight = false;
			bool HeadAgain = false;

			// Shown: passed to Update since the last OnUpdate. HeadStale: a repository change settled
			// while it was not, so its head is read again on its next Update.
			bool Shown = false;
			bool HeadStale = false;

			// The last blame failure logged for the path, so a lasting one is logged once.
			std::optional<std::string> LoggedError;

			std::optional<std::size_t> Revision;
			float QuietFor = 0.0f;

			bool Pending = false;

			std::uint64_t AuthorsRequested = 0;
			Core::Ref<const LineAuthors> Authors;
		};

		struct HistoryEntry
		{
			std::uint64_t Requested = 0;
			Core::LineHistoryRequest Request;
			LineHistoryState State;
		};

		void DropAuthors(Entry& entry);
		// Retires any overlay in flight first, so it cannot put its table back over this one.
		void PublishWithoutHistory(Entry& entry, Core::Ref<const HeadBlame> head);
		void RequestHead(const std::filesystem::path& path, Entry& entry);
		void RequestAuthors(const std::filesystem::path& path, Entry& entry,
							const Core::TextBuffer& buffer);
		void Deliver(std::function<void()> delivery);
		void MarkDirty();
		Entry* FindEntry(const std::filesystem::path& path);
		const Entry* FindEntry(const std::filesystem::path& path) const;

		BlamePort& m_Port;
		JobSubmitter m_Submit;

		std::map<std::filesystem::path, Entry> m_Entries;
		std::map<std::filesystem::path, HistoryEntry> m_Histories;

		// WARNING: Never reset, so a generation handed out before a Clear can never match one
		// handed out after it.
		std::uint64_t m_Generation = 0;

		Debouncer m_Debounce;
		DeliveryInbox m_Inbox;
	};
}
