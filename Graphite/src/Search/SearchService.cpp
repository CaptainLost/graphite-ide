#include "Search/SearchService.h"

#include "Core/Base/Log.h"
#include "Core/Git/Repository.h"
#include "Core/Text/Codec.h"
#include "Core/Text/FileProbe.h"
#include "Search/FileEnumerator.h"
#include "Search/Matcher.h"

#include <fstream>
#include <iterator>
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
			std::error_code ec;

			for (const UnsavedFile& file : unsaved)
			{
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
		// Before the queue joins: a running search polls its token between files, so cancelling
		// first turns a long shutdown into a short one.
		m_Token.Cancel();
	}

	void SearchService::Cancel()
	{
		m_Token.Cancel();

		const std::lock_guard<std::mutex> lock(m_Shared->Mutex);
		m_Shared->Progress.Running = false;
	}

	void SearchService::Start(SearchStart request)
	{
		m_Token.Cancel();
		m_Token = Core::CancellationToken{};

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
					// clear and survives into the new search's results. No test reaches this
					// window — it is narrower than a lock hand-off, not a race a sleep can widen —
					// so do not delete this guard because a passing suite implies it is unused.
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
				};

				const auto scanFile = [&](const std::filesystem::path& file)
				{
					if (token.IsCancelled())
					{
						return;
					}

					if (!MaskAccepts(input.Query.Mask, file.filename().string()))
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
