#pragma once

#include "Core/Base/Base.h"

#include "Git/Changes/ChangeBases.h"
#include "Git/Changes/ChangeMarkers.h"
#include "Git/DeliveryInbox.h"
#include "Git/Debouncer.h"
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

	// WARNING: Every job captures `this`, so the queue behind the submitter must be destroyed, and
	// its workers joined, before this object is. An owner holding both declares this BEFORE the
	// queue.
	class ChangeMarkersModel : Core::NonCopyable
	{
	public:
		ChangeMarkersModel(ChangeBasePort& port, JobSubmitter submit);

		void SubscribeTo(Core::EventBus& events);

		void OnUpdate(Core::Timestep timestep);

		// WARNING: A hidden document is not passed to Update, so its buffer is neither copied nor
		// diffed until it is shown again; its bases are still re-read when the repository changes.
		void Update(const std::filesystem::path& path, const Core::TextBuffer& buffer);

		void Retain(const std::function<bool(const std::filesystem::path&)>& isOpen);

		void Clear();

		void Invalidate();

		Core::Ref<const ChangeMarkers> Get(const std::filesystem::path& path) const;

		static constexpr float kEditQuietSeconds = 0.1f;

	private:
		struct BaseTexts
		{
			ChangeBases Texts;
			std::vector<Core::LineHunk> HeadToIndex;
		};

		struct Entry
		{
			std::uint64_t BasesRequested = 0;
			Core::Ref<const BaseTexts> Bases;

			std::optional<std::size_t> Revision;
			float QuietFor = 0.0f;

			bool Pending = false;

			std::uint64_t MarkersRequested = 0;
			Core::Ref<const ChangeMarkers> Markers;
		};

		void RequestBases(const std::filesystem::path& path, Entry& entry);
		void RequestMarkers(const std::filesystem::path& path, Entry& entry,
							const Core::TextBuffer& buffer);
		void Deliver(std::function<void()> delivery);
		void MarkDirty();
		Entry* FindEntry(const std::filesystem::path& path);
		const Entry* FindEntry(const std::filesystem::path& path) const;

		ChangeBasePort& m_Port;
		JobSubmitter m_Submit;

		std::map<std::filesystem::path, Entry> m_Entries;

		// WARNING: Never reset, so a generation handed out before a Clear can never match one
		// handed out after it.
		std::uint64_t m_Generation = 0;

		Debouncer m_Debounce;
		DeliveryInbox m_Inbox;
	};
}
