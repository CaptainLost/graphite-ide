#include "Sessions/SessionIndex.h"

#include "Core/Base/Base.h"

#include "Core/App/Redraw.h"

#include <set>

namespace Graphite {

	namespace {
		// A directory the watcher reports may carry a trailing separator; filename() is then
		// empty and every lexically_relative comparison below would count one path element too
		// many, splitting what should be "this project" into "this project" plus a phantom
		// empty child. parent_path() drops exactly that trailing separator and nothing else —
		// applied twice it is a no-op, since a path with no trailing separator already has a
		// non-empty filename().
		std::filesystem::path WithoutTrailingSeparator(const std::filesystem::path& path)
		{
			return path.filename().empty() && path.has_relative_path() ? path.parent_path() : path;
		}

		std::vector<std::filesystem::path> ProjectDirectories(const std::filesystem::path& projects)
		{
			std::vector<std::filesystem::path> directories;
			std::error_code status;

			for (std::filesystem::directory_iterator it{projects, status}, end; !status && it != end;
				 it.increment(status))
			{
				if (it->is_directory(status))
				{
					directories.push_back(it->path());
				}
			}

			return directories;
		}

		// Re-summarises `file` if its fingerprint moved and records what was delivered; a file
		// that is gone and was known is reported as removed. A file that cannot be read is
		// neither delivered nor recorded, so the watcher's next report of it tries again.
		void Examine(const std::filesystem::path& file, std::map<std::filesystem::path, Fingerprint>& prints,
					 IndexDelivery& out)
		{
			std::error_code status;

			if (!std::filesystem::exists(file, status))
			{
				if (prints.erase(file) > 0)
				{
					out.Removed.push_back(file);
				}
				return;
			}

			const Fingerprint print = FingerprintOf(file);
			const auto known = prints.find(file);

			if (known != prints.end() && known->second == print)
			{
				return;
			}

			std::optional<SessionSummary> summary = TrySummariseSession(file);

			if (!summary.has_value())
			{
				return;
			}

			prints[file] = summary->Print;
			out.Updated.push_back(std::move(*summary));
		}
	}

	std::vector<std::filesystem::path> SessionFilesUnder(const std::filesystem::path& projectDirectory)
	{
		std::vector<std::filesystem::path> files;
		std::error_code status;

		for (std::filesystem::directory_iterator it{projectDirectory, status}, end; !status && it != end;
			 it.increment(status))
		{
			if (it->is_regular_file(status) && it->path().extension() == ".jsonl")
			{
				files.push_back(it->path());
			}
		}

		return files;
	}

	std::vector<std::filesystem::path> ScanProjects(const std::filesystem::path& projects,
													const std::vector<SessionSummary>& cached,
													const Core::CancellationToken& token,
													const std::function<void(SessionSummary)>& onSummary)
	{
		std::map<std::filesystem::path, const SessionSummary*> byFile;

		for (const SessionSummary& summary : cached)
		{
			byFile[summary.File] = &summary;
		}

		for (const std::filesystem::path& project : ProjectDirectories(projects))
		{
			for (const std::filesystem::path& file : SessionFilesUnder(project))
			{
				if (token.IsCancelled())
				{
					return {};
				}

				const auto found = byFile.find(file);

				if (found != byFile.end() && found->second->Print == FingerprintOf(file))
				{
					onSummary(*found->second);
				}
				else if (std::optional<SessionSummary> summary = TrySummariseSession(file))
				{
					onSummary(std::move(*summary));
				}

				// Found, whether or not it could be read: an unreadable file is still there, and
				// its cached summary is the best there is until it reads again.
				if (found != byFile.end())
				{
					byFile.erase(found);
				}
			}
		}

		if (token.IsCancelled())
		{
			return {};
		}

		std::vector<std::filesystem::path> gone;
		gone.reserve(byFile.size());

		for (const auto& [file, summary] : byFile)
		{
			gone.push_back(file);
		}

		return gone;
	}

	std::optional<std::filesystem::path> SessionFileFor(const std::filesystem::path& projects,
														const std::filesystem::path& changedDirectory)
	{
		const std::filesystem::path relative =
			WithoutTrailingSeparator(changedDirectory).lexically_relative(projects);

		auto part = relative.begin();

		if (relative.empty() || *part == "..")
		{
			return std::nullopt;
		}

		const std::filesystem::path project = *part++;

		if (part == relative.end())
		{
			return std::nullopt;
		}

		const std::filesystem::path session = *part;
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

			// Covers whatever the work delivered last, and the end of scanning itself.
			Core::Redraw::Request();
		});
	}

	void SessionIndex::Start(std::filesystem::path projects, std::vector<SessionSummary> cached)
	{
		m_Token.Cancel();
		m_Token = Core::CancellationToken{};
		m_Inbox = Core::CreateRef<Inbox>();
		m_Known = Core::CreateRef<Known>();
		m_Projects = projects;

		Submit([projects = std::move(projects), cached = std::move(cached), token = m_Token](Inbox& inbox, Known& known) {
			std::vector<std::filesystem::path> gone =
				ScanProjects(projects, cached, token, [&inbox, &known](SessionSummary summary) {
					known.Prints[summary.File] = summary.Print;

					// Delivered one at a time, so the list fills while the scan runs.
					{
						const std::lock_guard<std::mutex> guard(inbox.Mutex);
						inbox.Ready.Updated.push_back(std::move(summary));
					}

					Core::Redraw::Request();
				});

			// The caller shows the cache before the scan confirms it, so a session whose file
			// went away while nothing was watching must be taken back out explicitly; no
			// watcher report will ever name it.
			const std::lock_guard<std::mutex> guard(inbox.Mutex);
			for (std::filesystem::path& file : gone)
			{
				inbox.Ready.Removed.push_back(std::move(file));
			}
		});
	}

	void SessionIndex::Refresh(std::vector<std::filesystem::path> changedDirectories)
	{
		Submit([projects = m_Projects, directories = std::move(changedDirectories)](Inbox& inbox, Known& known) {
			std::set<std::filesystem::path> files;

			for (const std::filesystem::path& raw : directories)
			{
				const std::filesystem::path directory = WithoutTrailingSeparator(raw);

				if (std::optional<std::filesystem::path> session = SessionFileFor(projects, directory))
				{
					files.insert(*session);
					continue;
				}

				// A project directory, or `projects` itself: everything under it, plus every
				// session known there, so a deleted one is noticed.
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

				for (const auto& [file, print] : known.Prints)
				{
					if (everything || file.parent_path() == directory)
					{
						files.insert(file);
					}
				}
			}

			IndexDelivery delivery;

			for (const std::filesystem::path& file : files)
			{
				Examine(file, known.Prints, delivery);
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

	void SessionIndex::Take(IndexDelivery& out)
	{
		const std::lock_guard<std::mutex> guard(m_Inbox->Mutex);

		for (SessionSummary& summary : m_Inbox->Ready.Updated)
		{
			out.Updated.push_back(std::move(summary));
		}
		for (std::filesystem::path& file : m_Inbox->Ready.Removed)
		{
			out.Removed.push_back(std::move(file));
		}

		m_Inbox->Ready = {};
	}
}
