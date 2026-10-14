#include "Git/Changes/ChangeMarkersModel.h"

#include "Core/Base/Base.h"

#include "Core/App/Redraw.h"
#include "Core/Base/Log.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/Git/Repository.h"
#include "Core/Git/TextDiff.h"
#include "Core/Text/TextBuffer.h"
#include "Workspace/Workspace.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

		events.Subscribe<RepositoryChangedEvent>([this](const RepositoryChangedEvent&) { MarkDirty(); });
	}

	void ChangeMarkersModel::Invalidate()
	{
		MarkDirty();
	}

	void ChangeMarkersModel::MarkDirty()
	{
		m_Debounce.Mark();
	}

	void ChangeMarkersModel::Deliver(std::function<void()> delivery)
	{
		m_Inbox.Post(std::move(delivery));
	}

	void ChangeMarkersModel::OnUpdate(Core::Timestep timestep)
	{
		const float seconds = timestep.GetSeconds();

		if (m_Debounce.Advance(timestep))
		{
			for (auto& [path, entry] : m_Entries)
			{
				RequestBases(path, entry);
			}
		}

		for (auto& [path, entry] : m_Entries)
		{
			entry.QuietFor += seconds;
		}

		m_Inbox.Drain();
	}

	void ChangeMarkersModel::Update(const std::filesystem::path& path, const Core::TextBuffer& buffer)
	{
		const bool inserted = !m_Entries.contains(path);
		Entry& entry = m_Entries[path];

		if (inserted)
		{
			RequestBases(path, entry);
		}

		const std::size_t revision = buffer.GetRevision();

		if (entry.Revision != revision)
		{
			const bool firstSeen = !entry.Revision.has_value();

			entry.Revision = revision;
			entry.Pending = true;
			entry.QuietFor = firstSeen ? kEditQuietSeconds : 0.0f;
		}

		if (entry.Pending && entry.Bases != nullptr && entry.QuietFor >= kEditQuietSeconds)
		{
			entry.Pending = false;
			RequestMarkers(path, entry, buffer);
		}
		else if (entry.Pending && entry.QuietFor < kEditQuietSeconds)
		{
			// WARNING: A revision can change on a reload from disk, which keeps the loop drawing
			// for only a few frames; the frame that ends the quiet period has to be asked for.
			Core::Redraw::RequestIn(
				std::chrono::duration<double>(kEditQuietSeconds - entry.QuietFor));
		}
	}

	void ChangeMarkersModel::RequestBases(const std::filesystem::path& path, Entry& entry)
	{
		const std::uint64_t generation = ++m_Generation;
		entry.BasesRequested = generation;

		m_Submit([this, path, generation]() {
			Core::Result<ChangeBases> read = m_Port.ReadBases(path);

			Core::Ref<const BaseTexts> bases;
			std::optional<Core::Error> error;

			if (read.HasValue())
			{
				Core::Ref<BaseTexts> fresh = Core::CreateRef<BaseTexts>();
				fresh->Texts = std::move(read).GetValue();

				if (fresh->Texts.Tracked && !fresh->Texts.Unusable && fresh->Texts.Index)
				{
					const std::string_view head =
						fresh->Texts.Head ? std::string_view{*fresh->Texts.Head} : std::string_view{};

					Core::Result<std::vector<Core::LineHunk>> hunks =
						Core::DiffTexts(head, *fresh->Texts.Index);

					if (hunks.HasValue())
					{
						fresh->HeadToIndex = std::move(hunks).GetValue();
					}
					else
					{
						error = hunks.GetError();
					}
				}

				if (!error)
				{
					bases = std::move(fresh);
				}
			}
			else
			{
				error = read.GetError();
			}

			Deliver([this, path, generation, bases = std::move(bases), error = std::move(error)]() {
				Entry* found = FindEntry(path);

				if (found == nullptr || found->BasesRequested != generation)
				{
					return;
				}

				Entry& target = *found;

				if (error)
				{
					CORE_WARN("Cannot read the git bases for '{}': {}", Core::ToUtf8(path), error->Message);
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

		if (buffer.GetLength() > Core::MaximumDiffTextSize)
		{
			entry.Markers.reset();
			return;
		}

		const std::uint64_t generation = ++m_Generation;
		entry.MarkersRequested = generation;

		m_Submit([this, path, generation, bases = entry.Bases, text = buffer.Materialize(),
				  lines = buffer.GetLineCount()]() {
			const std::string_view index =
				bases->Texts.Index ? std::string_view{*bases->Texts.Index} : std::string_view{};

			Core::Result<std::vector<Core::LineHunk>> hunks = Core::DiffTexts(index, text);

			if (!hunks.HasValue())
			{
				CORE_WARN("Cannot diff '{}' against the index: {}", Core::ToUtf8(path), hunks.GetError().Message);
				return;
			}

			const OldTexts oldTexts{
				.Head = bases->Texts.Head ? std::string_view{*bases->Texts.Head} : std::string_view{},
				.Index = index};

			Core::Ref<const ChangeMarkers> markers = Core::CreateRef<const ChangeMarkers>(
				BuildChangeMarkers(bases->HeadToIndex, hunks.GetValue(), lines, oldTexts));

			Deliver([this, path, generation, markers = std::move(markers)]() {
				Entry* found = FindEntry(path);

				if (found == nullptr || found->MarkersRequested != generation)
				{
					return;
				}

				found->Markers = markers;
			});
		});
	}

	void ChangeMarkersModel::Retain(const std::function<bool(const std::filesystem::path&)>& isOpen)
	{
		std::vector<std::filesystem::path> closedPaths;

		for ([[maybe_unused]] const auto& [entryPath, entry] : m_Entries)
		{
			if (!isOpen(entryPath))
			{
				closedPaths.push_back(entryPath);
			}
		}

		for (const std::filesystem::path& closedPath : closedPaths)
		{
			m_Entries.erase(closedPath);
		}
	}

	void ChangeMarkersModel::Clear()
	{
		m_Entries.clear();
		m_Debounce.Reset();
	}

	Core::Ref<const ChangeMarkers> ChangeMarkersModel::Get(const std::filesystem::path& path) const
	{
		const Entry* found = FindEntry(path);
		return found == nullptr ? nullptr : found->Markers;
	}

	ChangeMarkersModel::Entry* ChangeMarkersModel::FindEntry(const std::filesystem::path& path)
	{
		return m_Entries.contains(path) ? &m_Entries.at(path) : nullptr;
	}

	const ChangeMarkersModel::Entry* ChangeMarkersModel::FindEntry(
		const std::filesystem::path& path) const
	{
		return m_Entries.contains(path) ? &m_Entries.at(path) : nullptr;
	}
}
