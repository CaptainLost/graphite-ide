#pragma once

#include "Git/ChangeBases.h"
#include "Git/ChangeMarkers.h"
#include "Git/GitModel.h"

#include "Core/Base/NonCopyable.h"
#include "Core/Base/Timestep.h"
#include "Core/Events/EventBus.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace Core {

	class TextBuffer;
}

namespace Graphite {

	// What the gutter draws for each open document, and when it is recomputed.
	//
	// The rules are GitModel's:
	// - results are immutable and handed out by shared pointer;
	// - every request takes a generation from one model-wide counter, and only the newest
	//   request's answer is applied, so a slow job cannot put an older picture back;
	// - answers become visible only in OnUpdate, on the UI thread.
	//
	// Kept apart from GitModel because its state is per document and driven by the buffer's
	// revision, while GitModel's is per repository.
	//
	// Every job captures `this`, so the queue behind the submitter must be destroyed, and its
	// workers joined, before this object is. An owner holding both declares this BEFORE the
	// queue.
	class ChangeMarkersModel : Core::NonCopyable
	{
	public:
		ChangeMarkersModel(ChangeBasePort& port, JobSubmitter submit);

		void SubscribeTo(Core::EventBus& events);

		// Advances the timers and applies whatever the workers finished.
		void OnUpdate(Core::Timestep ts);

		// Called every frame for each document on screen. The first call for a path starts
		// reading its bases. After an edit, the buffer is copied and diffed once it has been
		// quiet for kEditQuietSeconds. A hidden document is not called, and so costs nothing
		// until it is shown again.
		void Update(const std::filesystem::path& path, const Core::TextBuffer& buffer);

		// Forgets every path the predicate rejects, and with it any answer still in flight.
		void Retain(const std::function<bool(const std::filesystem::path&)>& isOpen);

		void Clear();

		// Null while nothing has been computed, and for a file git does not track, cannot diff
		// or failed to read. In every case the column is empty.
		std::shared_ptr<const ChangeMarkers> Get(const std::filesystem::path& path) const;

		static constexpr float kEditQuietSeconds = 0.1f;

		// The same coalescing GitModel applies to the same events, for the same reasons.
		static constexpr float kDebounceSeconds = 0.15f;
		static constexpr float kMaxDebounceSeconds = 1.0f;

	private:
		struct Bases
		{
			ChangeBases Texts;
			std::vector<Core::LineHunk> HeadToIndex;
		};

		struct Entry
		{
			std::uint64_t BasesRequested = 0;
			std::shared_ptr<const Bases> Bases;

			// The revision Update last saw, and how long the buffer has sat at it.
			std::optional<std::size_t> Revision;
			float QuietFor = 0.0f;

			// Set by an edit or by fresh bases, cleared when the diff is submitted.
			bool Pending = false;

			std::uint64_t MarkersRequested = 0;
			std::shared_ptr<const ChangeMarkers> Markers;
		};

		void RequestBases(const std::filesystem::path& path, Entry& entry);
		void RequestMarkers(const std::filesystem::path& path, Entry& entry,
							const Core::TextBuffer& buffer);
		void Deliver(std::function<void()> delivery);
		void MarkDirty();

		ChangeBasePort& m_Port;
		JobSubmitter m_Submit;

		std::map<std::filesystem::path, Entry> m_Entries;

		// Never reset, so a generation handed out before a Clear can never match one handed out
		// after it.
		std::uint64_t m_Generation = 0;

		bool m_Dirty = false;
		float m_DirtyFor = 0.0f;
		float m_DirtySince = 0.0f;

		std::mutex m_InboxMutex;
		std::vector<std::function<void()>> m_Inbox;
	};
}
