#include "Git/ChangeMarkersModel.h"

#include "Core/Base/Log.h"
#include "Core/Git/Repository.h"
#include "Core/Git/TextDiff.h"
#include "Core/Text/TextBuffer.h"
#include "Workspace/Workspace.h"

#include <string>
#include <string_view>
#include <utility>

namespace Graphite {

	ChangeMarkersModel::ChangeMarkersModel(ChangeBasePort& port, JobSubmitter submit)
		: m_Port(port), m_Submit(std::move(submit))
	{
	}

	void ChangeMarkersModel::SubscribeTo(Core::EventBus& events)
	{
		events.Subscribe<WorkspaceOpenedEvent>([this](const WorkspaceOpenedEvent&) { Clear(); });
		events.Subscribe<WorkspaceClosedEvent>([this](const WorkspaceClosedEvent&) { Clear(); });

		events.Subscribe<WorkspaceFilesChangedEvent>(
			[this](const WorkspaceFilesChangedEvent&) { MarkDirty(); });

		// A commit, a checkout or a stage moves HEAD or the index under every open document.
		events.Subscribe<RepositoryChangedEvent>([this](const RepositoryChangedEvent&) { MarkDirty(); });
	}

	void ChangeMarkersModel::Invalidate()
	{
		MarkDirty();
	}

	void ChangeMarkersModel::MarkDirty()
	{
		// The burst's start is reset only when it was not already dirty, so a sustained stream
		// of events still hits the cap.
		if (!m_Dirty)
			m_DirtySince = 0.0f;

		m_Dirty = true;
		m_DirtyFor = 0.0f;
	}

	void ChangeMarkersModel::Deliver(std::function<void()> delivery)
	{
		const std::lock_guard<std::mutex> lock{m_InboxMutex};
		m_Inbox.push_back(std::move(delivery));
	}

	void ChangeMarkersModel::OnUpdate(Core::Timestep ts)
	{
		const float seconds = ts.GetSeconds();

		if (m_Dirty)
		{
			m_DirtyFor += seconds;
			m_DirtySince += seconds;

			if (m_DirtyFor >= kDebounceSeconds || m_DirtySince >= kMaxDebounceSeconds)
			{
				m_Dirty = false;
				m_DirtyFor = 0.0f;
				m_DirtySince = 0.0f;

				for (auto& [path, entry] : m_Entries)
					RequestBases(path, entry);
			}
		}

		for (auto& [path, entry] : m_Entries)
			entry.QuietFor += seconds;

		std::vector<std::function<void()>> ready;

		{
			const std::lock_guard<std::mutex> lock{m_InboxMutex};
			ready.swap(m_Inbox);
		}

		for (const auto& delivery : ready)
			delivery();
	}

	void ChangeMarkersModel::Update(const std::filesystem::path& path, const Core::TextBuffer& buffer)
	{
		auto [it, inserted] = m_Entries.try_emplace(path);
		Entry& entry = it->second;

		if (inserted)
			RequestBases(path, entry);

		const std::size_t revision = buffer.GetRevision();

		if (entry.Revision != revision)
		{
			// A document seen for the first time has nothing to wait out: it is not being typed
			// into, it has just been opened.
			const bool first = !entry.Revision.has_value();

			entry.Revision = revision;
			entry.Pending = true;
			entry.QuietFor = first ? kEditQuietSeconds : 0.0f;
		}

		if (entry.Pending && entry.Bases != nullptr && entry.QuietFor >= kEditQuietSeconds)
		{
			entry.Pending = false;
			RequestMarkers(path, entry, buffer);
		}
	}

	void ChangeMarkersModel::RequestBases(const std::filesystem::path& path, Entry& entry)
	{
		const std::uint64_t generation = ++m_Generation;
		entry.BasesRequested = generation;

		m_Submit([this, path, generation]() {
			auto read = m_Port.ReadBases(path);

			std::shared_ptr<const BaseTexts> bases;
			std::optional<Core::Error> error;

			if (read.HasValue())
			{
				auto fresh = std::make_shared<BaseTexts>();
				fresh->Texts = std::move(read).GetValue();

				// Diffed once here rather than on every keystroke: HEAD and the index change only
				// when git does.
				if (fresh->Texts.Tracked && !fresh->Texts.Unusable && fresh->Texts.Index)
				{
					const std::string_view head =
						fresh->Texts.Head ? std::string_view{*fresh->Texts.Head} : std::string_view{};

					auto hunks = Core::DiffTexts(head, *fresh->Texts.Index);

					if (hunks.HasValue())
						fresh->HeadToIndex = std::move(hunks).GetValue();
					else
						error = hunks.GetError();
				}

				if (!error)
					bases = std::move(fresh);
			}
			else
			{
				error = read.GetError();
			}

			Deliver([this, path, generation, bases = std::move(bases), error = std::move(error)]() {
				const auto found = m_Entries.find(path);

				if (found == m_Entries.end() || found->second.BasesRequested != generation)
					return;

				Entry& target = found->second;

				if (error)
				{
					CORE_WARN("Cannot read the git bases for '{}': {}", path.string(), error->Message);
					target.Bases.reset();
					target.Markers.reset();
					return;
				}

				target.Bases = bases;
				target.Pending = true;
				target.QuietFor = kEditQuietSeconds;
			});
		});
	}

	void ChangeMarkersModel::RequestMarkers(const std::filesystem::path& path, Entry& entry,
											const Core::TextBuffer& buffer)
	{
		if (!entry.Bases->Texts.Tracked || entry.Bases->Texts.Unusable)
		{
			entry.Markers.reset();
			return;
		}

		// The same ceiling the bases are read under. Checked before the copy, so a large
		// untracked file, or a tracked one grown past it, is neither copied on this thread nor
		// diffed on every quiet edit.
		if (buffer.GetLength() > Core::MaximumDiffTextSize)
		{
			entry.Markers.reset();
			return;
		}

		const std::uint64_t generation = ++m_Generation;
		entry.MarkersRequested = generation;

		// Copied here, on the UI thread, so the worker never touches the buffer.
		m_Submit([this, path, generation, bases = entry.Bases, text = buffer.Materialize(),
				  lines = buffer.GetLineCount()]() {
			// A file the index does not hold is new in its entirety: untracked, or removed from
			// the index while still open.
			const std::string_view index =
				bases->Texts.Index ? std::string_view{*bases->Texts.Index} : std::string_view{};

			auto hunks = Core::DiffTexts(index, text);

			if (!hunks.HasValue())
			{
				CORE_WARN("Cannot diff '{}' against the index: {}", path.string(), hunks.GetError().Message);
				return;
			}

			// The same two texts the diffs were taken against, so the tooltip's old lines are the
			// lines the hunks actually replaced.
			const OldTexts old{
				.Head = bases->Texts.Head ? std::string_view{*bases->Texts.Head} : std::string_view{},
				.Index = index};

			auto markers = std::make_shared<const ChangeMarkers>(
				BuildChangeMarkers(bases->HeadToIndex, hunks.GetValue(), lines, old));

			Deliver([this, path, generation, markers = std::move(markers)]() {
				const auto found = m_Entries.find(path);

				if (found == m_Entries.end() || found->second.MarkersRequested != generation)
					return;

				found->second.Markers = markers;
			});
		});
	}

	void ChangeMarkersModel::Retain(const std::function<bool(const std::filesystem::path&)>& isOpen)
	{
		std::erase_if(m_Entries, [&isOpen](const auto& item) { return !isOpen(item.first); });
	}

	void ChangeMarkersModel::Clear()
	{
		m_Entries.clear();
		m_Dirty = false;
		m_DirtyFor = 0.0f;
		m_DirtySince = 0.0f;
	}

	std::shared_ptr<const ChangeMarkers> ChangeMarkersModel::Get(const std::filesystem::path& path) const
	{
		const auto found = m_Entries.find(path);
		return found == m_Entries.end() ? nullptr : found->second.Markers;
	}
}
