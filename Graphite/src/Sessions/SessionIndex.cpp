#include "Sessions/SessionIndex.h"

#include "Core/Base/Base.h"

#include "Core/App/Redraw.h"

#include <set>

namespace Graphite {

	namespace {
		// WARNING: A directory the watcher reports may end in a separator, which makes lexically_relative count
		// one phantom empty element. parent_path() of such a path drops exactly that separator.
		std::filesystem::path WithoutTrailingSeparator(const std::filesystem::path& path)
		{
			return path.filename().empty() && path.has_relative_path() ? path.parent_path() : path;
		}

		std::vector<std::filesystem::path> ProjectDirectories(const std::filesystem::path& projects)
		{
			std::vector<std::filesystem::path> directories;
			std::error_code status;

			for (std::filesystem::directory_iterator entryIterator{projects, status}, endIterator;
				 !status && entryIterator != endIterator; entryIterator.increment(status))
			{
				if (entryIterator->is_directory(status))
				{
					directories.push_back(entryIterator->path());
				}
			}

			return directories;
		}

		void Examine(const std::filesystem::path& file, FingerprintsByFile& prints, IndexDelivery& delivery)
		{
			std::error_code status;

			if (!std::filesystem::exists(file, status))
			{
				if (prints.erase(file) > 0)
				{
					delivery.Removed.push_back(file);
				}
				return;
			}

			const Fingerprint print = FingerprintOf(file);
			const bool isUnchanged = prints.contains(file) && prints.at(file) == print;

			if (isUnchanged)
			{
				return;
			}

			std::optional<SessionSummary> summary = TrySummariseSession(file);

			if (!summary.has_value())
			{
				return;
			}

			prints[file] = summary->Print;
			delivery.Updated.push_back(std::move(*summary));
		}
	}

	std::vector<std::filesystem::path> SessionFilesUnder(const std::filesystem::path& projectDirectory)
	{
		std::vector<std::filesystem::path> files;
		std::error_code status;

		for (std::filesystem::directory_iterator entryIterator{projectDirectory, status}, endIterator;
			 !status && entryIterator != endIterator; entryIterator.increment(status))
		{
			if (entryIterator->is_regular_file(status) && entryIterator->path().extension() == ".jsonl")
			{
				files.push_back(entryIterator->path());
			}
		}

		return files;
	}

	std::vector<std::filesystem::path> ScanProjects(const std::filesystem::path& projects,
													const std::vector<SessionSummary>& cached,
													const Core::CancellationToken& token,
													const std::function<void(SessionSummary)>& onSummary)
	{
		std::map<std::filesystem::path, const SessionSummary*> cachedSummaryByFile;

		for (const SessionSummary& summary : cached)
		{
			cachedSummaryByFile[summary.File] = &summary;
		}

		for (const std::filesystem::path& project : ProjectDirectories(projects))
		{
			for (const std::filesystem::path& file : SessionFilesUnder(project))
			{
				if (token.IsCancelled())
				{
					return {};
				}

				std::map<std::filesystem::path, const SessionSummary*>::node_type cachedNode =
					cachedSummaryByFile.extract(file);
				const bool isCached = !cachedNode.empty();

				if (isCached && cachedNode.mapped()->Print == FingerprintOf(file))
				{
					onSummary(*cachedNode.mapped());
				}
				else if (std::optional<SessionSummary> summary = TrySummariseSession(file))
				{
					onSummary(std::move(*summary));
				}
			}
		}

		if (token.IsCancelled())
		{
			return {};
		}

		std::vector<std::filesystem::path> vanishedFiles;
		vanishedFiles.reserve(cachedSummaryByFile.size());

		for (const auto& [file, summary] : cachedSummaryByFile)
		{
			vanishedFiles.push_back(file);
		}

		return vanishedFiles;
	}

	std::optional<std::filesystem::path> SessionFileFor(const std::filesystem::path& projects,
														const std::filesystem::path& changedDirectory)
	{
		const std::filesystem::path relative =
			WithoutTrailingSeparator(changedDirectory).lexically_relative(projects);

		std::filesystem::path::iterator pathElement = relative.begin();

		if (relative.empty() || *pathElement == "..")
		{
			return std::nullopt;
		}

		const std::filesystem::path project = *pathElement++;

		if (pathElement == relative.end())
		{
			return std::nullopt;
		}

		const std::filesystem::path session = *pathElement;
		return projects / project / (session.native() + L".jsonl");
	}

	SessionIndex::~SessionIndex()
	{
		m_Token.Cancel();
	}

	void SessionIndex::Submit(std::function<void(Inbox&, Known&)> work)
	{
		{
			const std::lock_guard<std::mutex> guard(m_Inbox->Mutex);
			++m_Inbox->Pending;
		}

		m_Jobs.Submit([inbox = m_Inbox, known = m_Known, token = m_Token, work = std::move(work)] {
			if (!token.IsCancelled())
			{
				work(*inbox, *known);
			}

			{
				const std::lock_guard<std::mutex> guard(inbox->Mutex);
				--inbox->Pending;
			}

			Core::Redraw::Request();
		});
	}

	void SessionIndex::DeliverScannedSummary(Inbox& inbox, Known& known, SessionSummary summary)
	{
		known.Fingerprints[summary.File] = summary.Print;

		{
			const std::lock_guard<std::mutex> guard(inbox.Mutex);
			inbox.Ready.Updated.push_back(std::move(summary));
		}

		++inbox.Scanned;
		Core::Redraw::Request();
	}

	void SessionIndex::Start(std::filesystem::path projects, std::vector<SessionSummary> cached)
	{
		m_Token.Cancel();
		m_Token = Core::CancellationToken{};
		m_Inbox = Core::CreateRef<Inbox>();
		m_Known = Core::CreateRef<Known>();
		m_Projects = projects;

		Submit([projects = std::move(projects), cached = std::move(cached), token = m_Token](Inbox& inbox, Known& known) {
			std::size_t sessionFileTotal = 0;
			for (const std::filesystem::path& project : ProjectDirectories(projects))
			{
				sessionFileTotal += SessionFilesUnder(project).size();
			}
			inbox.Total = sessionFileTotal;

			std::vector<std::filesystem::path> removedWhileNotWatching =
				ScanProjects(projects, cached, token, [&inbox, &known](SessionSummary summary) {
					DeliverScannedSummary(inbox, known, std::move(summary));
				});

			const std::lock_guard<std::mutex> guard(inbox.Mutex);
			for (std::filesystem::path& file : removedWhileNotWatching)
			{
				inbox.Ready.Removed.push_back(std::move(file));
			}

			// WARNING: The flag is set after everything above is in the inbox, so a reader that sees it and then
			// takes has the whole scan. A cancelled scan never sets it; the next Start drops its inbox anyway.
			if (!token.IsCancelled())
			{
				inbox.Scanned = inbox.Total.load();
				inbox.InitialScanDone = true;
			}
		});
	}

	void SessionIndex::Refresh(std::vector<std::filesystem::path> changedDirectories)
	{
		Submit([projects = m_Projects, directories = std::move(changedDirectories)](Inbox& inbox, Known& known) {
			std::set<std::filesystem::path> files;

			for (const std::filesystem::path& reportedDirectory : directories)
			{
				const std::filesystem::path directory = WithoutTrailingSeparator(reportedDirectory);

				if (std::optional<std::filesystem::path> session = SessionFileFor(projects, directory))
				{
					files.insert(*session);
					continue;
				}

				const std::filesystem::path relative = directory.lexically_relative(projects);
				const bool everything = relative.empty() || relative == ".";
				const std::vector<std::filesystem::path> projectDirectories =
					everything ? ProjectDirectories(projects) : std::vector<std::filesystem::path>{directory};

				for (const std::filesystem::path& project : projectDirectories)
				{
					for (const std::filesystem::path& file : SessionFilesUnder(project))
					{
						files.insert(file);
					}
				}

				for (const auto& [knownFile, fingerprint] : known.Fingerprints)
				{
					if (everything || knownFile.parent_path() == directory)
					{
						files.insert(knownFile);
					}
				}
			}

			IndexDelivery delivery;

			for (const std::filesystem::path& file : files)
			{
				Examine(file, known.Fingerprints, delivery);
			}

			const std::lock_guard<std::mutex> guard(inbox.Mutex);
			for (SessionSummary& summary : delivery.Updated)
			{
				inbox.Ready.Updated.push_back(std::move(summary));
			}
			for (std::filesystem::path& file : delivery.Removed)
			{
				inbox.Ready.Removed.push_back(std::move(file));
			}
		});
	}

	bool SessionIndex::IsScanning() const
	{
		const std::lock_guard<std::mutex> guard(m_Inbox->Mutex);
		return m_Inbox->Pending > 0;
	}

	ScanProgress SessionIndex::InitialScan() const
	{
		// WARNING: Done is read first. The counts only grow, so a reader that sees the flag sees the final ones.
		const bool done = m_Inbox->InitialScanDone.load();
		return {.Done = done, .Scanned = m_Inbox->Scanned.load(), .Total = m_Inbox->Total.load()};
	}

	void SessionIndex::Take(IndexDelivery& delivery)
	{
		const std::lock_guard<std::mutex> guard(m_Inbox->Mutex);

		for (SessionSummary& summary : m_Inbox->Ready.Updated)
		{
			delivery.Updated.push_back(std::move(summary));
		}
		for (std::filesystem::path& file : m_Inbox->Ready.Removed)
		{
			delivery.Removed.push_back(std::move(file));
		}

		m_Inbox->Ready = {};
	}
}
