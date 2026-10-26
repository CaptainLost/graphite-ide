#include "Git/Blame/BlameModel.h"

#include "Core/Base/Base.h"

#include "Core/App/Redraw.h"
#include "Core/Base/Log.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/Git/Repository.h"
#include "Core/Git/TextDiff.h"
#include "Core/Text/TextBuffer.h"
#include "Workspace/Workspace.h"

#include <chrono>
#include <string>
#include <utility>
#include <vector>

namespace Graphite {

	BlameModel::BlameModel(BlamePort& port, JobSubmitter submit)
		: m_Port(port), m_Submit(std::move(submit))
	{
	}

	void BlameModel::SubscribeTo(Core::EventBus& events)
	{
		events.Subscribe<WorkspaceOpenedEvent>([this](const WorkspaceOpenedEvent&) { Clear(); });
		events.Subscribe<WorkspaceClosedEvent>([this](const WorkspaceClosedEvent&) { Clear(); });

		// WARNING: Not WorkspaceFilesChangedEvent. HEAD's blame does not depend on the working tree,
		// and re-blaming every open file on each save costs seconds of worker time on a long history.
		events.Subscribe<RepositoryChangedEvent>([this](const RepositoryChangedEvent&) { MarkDirty(); });
	}

	void BlameModel::Invalidate()
	{
		MarkDirty();
	}

	void BlameModel::MarkDirty()
	{
		m_Debounce.Mark();
	}

	void BlameModel::Deliver(std::function<void()> delivery)
	{
		m_Inbox.Post(std::move(delivery));
	}

	void BlameModel::OnUpdate(Core::Timestep timestep)
	{
		const float seconds = timestep.GetSeconds();

		if (m_Debounce.Advance(timestep))
		{
			for (auto& [path, entry] : m_Entries)
			{
				RequestHead(path, entry);
			}
		}

		for (auto& [path, entry] : m_Entries)
		{
			entry.QuietFor += seconds;
		}

		m_Inbox.Drain();
	}

	void BlameModel::Update(const std::filesystem::path& path, const Core::TextBuffer& buffer)
	{
		const bool inserted = !m_Entries.contains(path);
		Entry& entry = m_Entries[path];

		if (inserted)
		{
			RequestHead(path, entry);
		}

		const std::size_t revision = buffer.GetRevision();

		if (entry.Revision != revision)
		{
			const bool firstSeen = !entry.Revision.has_value();

			entry.Revision = revision;
			entry.Pending = true;
			entry.QuietFor = firstSeen ? kEditQuietSeconds : 0.0f;
		}

		if (entry.Pending && entry.Head != nullptr && entry.QuietFor >= kEditQuietSeconds)
		{
			entry.Pending = false;
			RequestAuthors(path, entry, buffer);
		}
		else if (entry.Pending && entry.QuietFor < kEditQuietSeconds)
		{
			// WARNING: A revision can change on a reload from disk, which keeps the loop drawing
			// for only a few frames; the frame that ends the quiet period has to be asked for.
			Core::Redraw::RequestIn(
				std::chrono::duration<double>(kEditQuietSeconds - entry.QuietFor));
		}
	}

	void BlameModel::RequestHead(const std::filesystem::path& path, Entry& entry)
	{
		const std::uint64_t generation = ++m_Generation;
		entry.HeadRequested = generation;

		m_Submit([this, path, generation]() {
			Core::Result<HeadBlame> read = m_Port.BlameAtHead(path);

			Core::Ref<const HeadBlame> head;
			std::optional<Core::Error> error;

			if (read.HasValue())
			{
				head = Core::CreateRef<const HeadBlame>(std::move(read).GetValue());
			}
			else
			{
				error = read.GetError();
			}

			Deliver([this, path, generation, head = std::move(head), error = std::move(error)]() {
				Entry* found = FindEntry(path);

				if (found == nullptr || found->HeadRequested != generation)
				{
					return;
				}

				Entry& target = *found;

				if (error)
				{
					CORE_WARN("Cannot blame '{}' at HEAD: {}", Core::ToUtf8(path), error->Message);
					target.Head.reset();
					PublishWithoutHistory(target, Core::CreateRef<const HeadBlame>());
					return;
				}

				target.Head = head;
				target.Pending = true;
				target.QuietFor = kEditQuietSeconds;
			});
		});
	}

	void BlameModel::DropAuthors(Entry& entry)
	{
		// Retiring the generation keeps an overlay job already in flight from putting its table back.
		entry.AuthorsRequested = ++m_Generation;
		entry.Authors.reset();
	}

	void BlameModel::PublishWithoutHistory(Entry& entry, Core::Ref<const HeadBlame> head)
	{
		DropAuthors(entry);
		entry.Authors = Core::CreateRef<const LineAuthors>(LineAuthors{.Head = std::move(head)});
	}

	void BlameModel::RequestAuthors(const std::filesystem::path& path, Entry& entry,
									const Core::TextBuffer& buffer)
	{
		if (!entry.Head->Tracked || entry.Head->Unusable)
		{
			PublishWithoutHistory(entry, entry.Head);
			return;
		}

		// A tracked head the port gave no blame is read as untracked rather than left with no table at all.
		if (entry.Head->Blame == nullptr)
		{
			PublishWithoutHistory(entry, Core::CreateRef<const HeadBlame>());
			return;
		}

		if (buffer.GetLength() > Core::MaximumDiffTextSize)
		{
			DropAuthors(entry);
			return;
		}

		const std::uint64_t generation = ++m_Generation;
		entry.AuthorsRequested = generation;

		m_Submit([this, path, generation, head = entry.Head, text = buffer.Materialize(),
				  lines = buffer.GetLineCount()]() {
			Core::Result<std::vector<Core::LineHunk>> hunks = Core::DiffTexts(head->HeadText, text);

			if (!hunks.HasValue())
			{
				CORE_WARN("Cannot diff '{}' against HEAD: {}", Core::ToUtf8(path), hunks.GetError().Message);
				return;
			}

			Core::Ref<const LineAuthors> authors =
				Core::CreateRef<const LineAuthors>(BuildLineAuthors(head, hunks.GetValue(), lines));

			Deliver([this, path, generation, authors = std::move(authors)]() {
				Entry* found = FindEntry(path);

				if (found == nullptr || found->AuthorsRequested != generation)
				{
					return;
				}

				found->Authors = authors;
			});
		});
	}

	void BlameModel::RequestLineHistory(const std::filesystem::path& path,
										const Core::LineHistoryRequest& request)
	{
		const auto existing = m_Histories.find(path);

		if (existing != m_Histories.end() && existing->second.Request == request)
		{
			return;
		}

		const std::uint64_t generation = ++m_Generation;

		HistoryEntry& entry = m_Histories[path];
		entry.Requested = generation;
		entry.Request = request;
		entry.State = LineHistoryState{.InFlight = true};

		m_Submit([this, path, generation, request]() {
			Core::Result<Core::LineHistory> read = m_Port.QueryLineHistory(request);

			Core::Ref<const Core::LineHistory> history;
			std::optional<Core::Error> error;

			if (read.HasValue())
			{
				history = Core::CreateRef<const Core::LineHistory>(std::move(read).GetValue());
			}
			else
			{
				error = read.GetError();
			}

			Deliver([this, path, generation, history = std::move(history), error = std::move(error)]() {
				const auto found = m_Histories.find(path);

				if (found == m_Histories.end() || found->second.Requested != generation)
				{
					return;
				}

				found->second.State = LineHistoryState{history, error, false};
			});
		});
	}

	LineHistoryState BlameModel::GetLineHistory(const std::filesystem::path& path) const
	{
		const auto found = m_Histories.find(path);
		return found == m_Histories.end() ? LineHistoryState{} : found->second.State;
	}

	void BlameModel::ForgetLineHistory(const std::filesystem::path& path)
	{
		m_Histories.erase(path);
	}

	void BlameModel::Retain(const std::function<bool(const std::filesystem::path&)>& isOpen)
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

		closedPaths.clear();

		for ([[maybe_unused]] const auto& [historyPath, history] : m_Histories)
		{
			if (!isOpen(historyPath))
			{
				closedPaths.push_back(historyPath);
			}
		}

		for (const std::filesystem::path& closedPath : closedPaths)
		{
			m_Histories.erase(closedPath);
		}
	}

	void BlameModel::Clear()
	{
		m_Entries.clear();
		m_Histories.clear();
		m_Debounce.Reset();
	}

	Core::Ref<const LineAuthors> BlameModel::Get(const std::filesystem::path& path) const
	{
		const Entry* found = FindEntry(path);
		return found == nullptr ? nullptr : found->Authors;
	}

	BlameModel::Entry* BlameModel::FindEntry(const std::filesystem::path& path)
	{
		return m_Entries.contains(path) ? &m_Entries.at(path) : nullptr;
	}

	const BlameModel::Entry* BlameModel::FindEntry(const std::filesystem::path& path) const
	{
		return m_Entries.contains(path) ? &m_Entries.at(path) : nullptr;
	}
}
