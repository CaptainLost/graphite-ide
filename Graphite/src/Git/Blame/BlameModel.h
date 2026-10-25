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
		// diffed until it is shown again; its head blame is still re-read when the repository changes.
		void Update(const std::filesystem::path& path, const Core::TextBuffer& buffer);

		Core::Ref<const LineAuthors> Get(const std::filesystem::path& path) const;

		// Ignored when the same request is already held or in flight for the path.
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
