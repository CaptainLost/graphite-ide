#include "Search/SearchService.h"

#include "Core/App/Redraw.h"
#include "Core/Fs/FileSystem.h"
#include "Core/Git/Repository.h"
#include "Core/Text/Codec.h"
#include "Core/Text/FileProbe.h"
#include "Search/FileEnumerator.h"
#include "Search/Matcher.h"

#include <fstream>
#include <iterator>
#include <optional>
#include <system_error>
#include <utility>

namespace Graphite {

	namespace {

		std::optional<std::string> ReadAll(const std::filesystem::path& path)
		{
			std::ifstream in(path, std::ios::binary);

			if (!in)
			{
				return std::nullopt;
			}

			return std::string(std::istreambuf_iterator<char>(in),
							   std::istreambuf_iterator<char>());
		}

		const UnsavedFile* FindUnsaved(const std::vector<UnsavedFile>& unsaved,
									   const std::filesystem::path& path)
		{
			for (const UnsavedFile& file : unsaved)
			{
				// Two string comparisons before anything touches the disk. equivalent() stats
				// both of its operands, and asking it about every enumerated file against every
				// open tab is a stat pair per pairing — hundreds of thousands of them on a large
				// tree with a normal number of tabs open, which dominated the search. A file
				// whose name no open tab shares cannot be that tab's file, and the paths are
				// normalised once when the search starts so the equal case usually settles here.
				if (file.Path.filename() != path.filename())
				{
					continue;
				}

				if (file.Path == path)
				{
					return &file;
				}

				// Only for a same-named file at a different-looking path: a symlink, a
				// substituted drive, or a root spelled differently from the tab's path.
				std::error_code ec;

				if (std::filesystem::equivalent(file.Path, path, ec) && !ec)
				{
					return &file;
				}
			}

			return nullptr;
		}

		// Names what a scanned file resolved to, so a call site never hands three same-typed
		// counters to a lambda that could silently swap them.
		enum class FileOutcome
		{
			Scanned,
			Skipped,
			Unreadable
		};
	}

	SearchService::SearchService() = default;

	SearchService::~SearchService()
	{
		// Before the queue joins: a running search polls its token once per file, so cancelling
		// first bounds the wait to whatever file is already in flight — at most one LargeText
		// file's read, decode and scan — rather than however much of the search remains.
		m_Token.Cancel();
	}

	void SearchService::Cancel()
	{
		m_Token.Cancel();

		const std::lock_guard<std::mutex> lock(m_Shared->Mutex);

		// Bumping the generation here, not just flipping the flag, closes the same window the
		// guard in Start's contribute lambda closes: a worker between token polls can still be
		// mid-file when Cancel runs, and without this its eventual contribute call would still
		// match and land after the search was reported finished.
		++m_Shared->Generation;
		m_Shared->Progress.Running = false;
	}

	void SearchService::Start(SearchStart request)
	{
		m_Token.Cancel();
		m_Token = Core::CancellationToken{};

		// Refused here rather than in the matcher, which is where the emptiness is finally
		// noticed: by then every file in the tree has been enumerated, probed, read and
		// decoded, and only then found to match nothing. An empty query runs nothing.
		if (request.Query.Needle.empty())
		{
			const std::lock_guard<std::mutex> lock(m_Shared->Mutex);

			++m_Shared->Generation;

			m_Shared->Hits.clear();
			m_Shared->Progress = SearchProgress{};

			return;
		}

		// Once per open tab instead of once per pairing with an enumerated file: FindUnsaved
		// settles on a path comparison when both sides are spelled the same way.
		for (UnsavedFile& unsaved : request.Unsaved)
		{
			std::error_code ec;
			std::filesystem::path resolved = std::filesystem::weakly_canonical(unsaved.Path, ec);

			unsaved.Path = ec ? unsaved.Path.lexically_normal() : std::move(resolved);
		}

		std::uint64_t generation = 0;

		{
			const std::lock_guard<std::mutex> lock(m_Shared->Mutex);

			generation = ++m_Shared->Generation;

			m_Shared->Hits.clear();
			m_Shared->Progress = SearchProgress{};
			m_Shared->Progress.Running = true;
		}

		m_Jobs.Submit(
			[shared = m_Shared, token = m_Token, generation, input = std::move(request)]
			{
				// Everything this job writes goes through this, which drops the write when a
				// newer search has already taken over.
				const auto contribute = [&shared, generation](const std::vector<SearchHit>& found,
															  FileOutcome outcome)
				{
					const std::lock_guard<std::mutex> lock(shared->Mutex);

					// Covers the one window Start's own clear cannot: a contribution from the
					// previous search that is already inside this lock, blocked behind the
					// contribute call the newer Start is waiting to make, when the newer Start's
					// clear-and-bump runs. Without this check that contribution lands after the
					// clear and survives into the new search's results. The same check also
					// covers Cancel, which bumps the generation under this same mutex for exactly
					// this reason. No test reaches either window — narrower than a lock hand-off,
					// not a race a sleep can widen — so do not delete this guard because a
					// passing suite implies it is unused.
					if (shared->Generation != generation)
					{
						return;
					}

					shared->Hits.insert(shared->Hits.end(), found.begin(), found.end());

					switch (outcome)
					{
					case FileOutcome::Scanned:
						shared->Progress.FilesScanned += 1;
						break;
					case FileOutcome::Skipped:
						shared->Progress.FilesSkipped += 1;
						break;
					case FileOutcome::Unreadable:
						shared->Progress.Unreadable += 1;
						break;
					}

					Core::Redraw::Request();
				};

				const auto scanFile = [&](const std::filesystem::path& file)
				{
					if (token.IsCancelled())
					{
						return;
					}

					// ToUtf8, not string(): on MSVC the latter converts through the active code
					// page, so a mask typed as UTF-8 in the query field could never match a
					// filename with a non-ASCII character in it.
					if (!MaskAccepts(input.Query.Mask, Core::ToUtf8(file.filename())))
					{
						return;
					}

					std::vector<SearchHit> found;

					if (const UnsavedFile* open = FindUnsaved(input.Unsaved, file))
					{
						FindInText(input.Query, open->Text, file, found);
						contribute(found, FileOutcome::Scanned);
						return;
					}

					// Core::Result has an explicit operator bool and GetValue(), and NO
					// operator-> — reaching through it with -> does not compile.
					const Core::Result<Core::FileProbe> probed = Core::ProbeFile(file);

					if (!probed)
					{
						contribute({}, FileOutcome::Unreadable);
						return;
					}

					const Core::FileProbe& probe = probed.GetValue();

					if (probe.Kind == Core::FileKind::Binary ||
						probe.Kind == Core::FileKind::TooLarge)
					{
						contribute({}, FileOutcome::Skipped);
						return;
					}

					const std::optional<std::string> bytes = ReadAll(file);

					if (!bytes)
					{
						contribute({}, FileOutcome::Unreadable);
						return;
					}

					const Core::Result<std::string> text =
						Core::Decode(*bytes, probe.FileEncoding);

					if (!text)
					{
						contribute({}, FileOutcome::Unreadable);
						return;
					}

					FindInText(input.Query, text.GetValue(), file, found);
					contribute(found, FileOutcome::Scanned);
				};

				if (input.Options.ProjectsOnly)
				{
					for (const std::filesystem::path& file : input.ProjectFiles)
					{
						if (token.IsCancelled())
						{
							break;
						}

						scanFile(file);
					}
				}
				else
				{
					EnumerationInput enumeration;
					enumeration.Root = input.Root;

					// Opened here, on this thread, rather than shared with the Workspace's:
					// libgit2 handles are not safe to use from two threads, and the explorer
					// asks the Workspace's the same question every frame.
					std::optional<Core::Repository> repository;

					if (input.Options.RespectGitIgnore)
					{
						Core::Result<Core::Repository> opened =
							Core::Repository::Open(input.Root);

						if (opened)
						{
							repository.emplace(std::move(opened.GetValue()));

							enumeration.Ignored =
								[&repository](const std::filesystem::path& path, bool)
							{ return repository->IsIgnored(path); };
						}
					}

					EnumerateFiles(enumeration, token, scanFile);
				}

				const std::lock_guard<std::mutex> lock(shared->Mutex);

				if (shared->Generation == generation)
				{
					shared->Progress.Running = false;
				}

				Core::Redraw::Request();
			});
	}

	void SearchService::Drain(std::vector<SearchHit>& hits, SearchProgress& progress)
	{
		const std::lock_guard<std::mutex> lock(m_Shared->Mutex);

		hits.insert(hits.end(), std::make_move_iterator(m_Shared->Hits.begin()),
					std::make_move_iterator(m_Shared->Hits.end()));

		m_Shared->Hits.clear();

		progress = m_Shared->Progress;
	}
}
