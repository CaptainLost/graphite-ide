#include "Search/SearchService.h"

#include "Core/App/Redraw.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/Git/Repository.h"
#include "Core/Text/FileProbe.h"
#include "Search/FileEnumerator.h"
#include "Search/Matcher.h"

#include <iterator>
#include <optional>
#include <system_error>
#include <utility>

namespace Graphite {

	namespace {

		const UnsavedFile* FindUnsaved(const std::vector<UnsavedFile>& unsavedFiles,
									   const std::filesystem::path& path)
		{
			for (const UnsavedFile& unsavedFile : unsavedFiles)
			{
				// WARNING: Compare names before equivalent(), which stats both of its operands;
				// doing that for every file against every open tab dominated the search time.
				if (unsavedFile.Path.filename() != path.filename())
				{
					continue;
				}

				if (unsavedFile.Path == path)
				{
					return &unsavedFile;
				}

				std::error_code errorCode;

				if (std::filesystem::equivalent(unsavedFile.Path, path, errorCode) && !errorCode)
				{
					return &unsavedFile;
				}
			}

			return nullptr;
		}
	}

	SearchService::SearchService() = default;

	SearchService::~SearchService()
	{
		// WARNING: Cancel before m_Jobs joins, or the destructor waits for the whole search.
		m_Token.Cancel();
	}

	void SearchService::Cancel()
	{
		m_Token.Cancel();

		const std::lock_guard<std::mutex> lock(m_Shared->Mutex);

		++m_Shared->Generation;
		m_Shared->Progress.Running = false;
	}

	void SearchService::Contribute(Shared& shared, std::uint64_t generation,
								   const std::vector<SearchHit>& found, FileOutcome outcome)
	{
		const std::lock_guard<std::mutex> lock(shared.Mutex);

		// WARNING: This guard closes a lock hand-off window that no test reaches, so a passing
		// suite does not show it is unused. Start and Cancel bump the generation to drop a
		// contribution already waiting on this lock.
		if (shared.Generation != generation)
		{
			return;
		}

		shared.Hits.insert(shared.Hits.end(), found.begin(), found.end());

		switch (outcome)
		{
		case FileOutcome::Scanned:
			shared.Progress.FilesScanned += 1;
			break;
		case FileOutcome::Skipped:
			shared.Progress.FilesSkipped += 1;
			break;
		case FileOutcome::Unreadable:
			shared.Progress.Unreadable += 1;
			break;
		}

		Core::Redraw::Request();
	}

	void SearchService::ScanFile(const ScanContext& context, const std::filesystem::path& file)
	{
		if (context.Token.IsCancelled())
		{
			return;
		}

		if (!MaskAccepts(context.Input.Query.Mask, Core::ToUtf8(file.filename())))
		{
			return;
		}

		const std::vector<SearchHit> noHits;
		std::vector<SearchHit> found;

		if (const UnsavedFile* openDocument = FindUnsaved(context.Input.Unsaved, file))
		{
			FindInText(context.Input.Query, openDocument->Text, file, found);
			Contribute(context.SharedState, context.Generation, found, FileOutcome::Scanned);
			return;
		}

		const Core::DiskText read = Core::ReadDiskText(file);

		switch (read.Outcome)
		{
		case Core::DiskTextOutcome::Read:
			FindInText(context.Input.Query, read.Text, file, found);
			Contribute(context.SharedState, context.Generation, found, FileOutcome::Scanned);
			return;

		case Core::DiskTextOutcome::Binary:
		case Core::DiskTextOutcome::TooLarge:
			Contribute(context.SharedState, context.Generation, noHits, FileOutcome::Skipped);
			return;

		case Core::DiskTextOutcome::Unreadable:
		case Core::DiskTextOutcome::Undecodable:
			Contribute(context.SharedState, context.Generation, noHits,
					   FileOutcome::Unreadable);
			return;
		}
	}

	void SearchService::Start(SearchStart request)
	{
		m_Token.Cancel();
		m_Token = Core::CancellationToken{};

		const bool queryIsEmpty = request.Query.Needle.empty();

		if (queryIsEmpty)
		{
			const std::lock_guard<std::mutex> lock(m_Shared->Mutex);

			++m_Shared->Generation;

			m_Shared->Hits.clear();
			m_Shared->Progress = SearchProgress{};

			return;
		}

		for (UnsavedFile& unsavedFile : request.Unsaved)
		{
			std::error_code errorCode;
			std::filesystem::path resolved =
				std::filesystem::weakly_canonical(unsavedFile.Path, errorCode);

			unsavedFile.Path =
				errorCode ? unsavedFile.Path.lexically_normal() : std::move(resolved);
		}

		std::uint64_t generation = 0;

		{
			const std::lock_guard<std::mutex> lock(m_Shared->Mutex);

			generation = ++m_Shared->Generation;

			m_Shared->Hits.clear();
			m_Shared->Progress = SearchProgress{};
			m_Shared->Progress.Running = true;
		}

		m_Jobs.Submit([shared = m_Shared, token = m_Token, generation,
					   input = std::move(request)] {
			const ScanContext context{.SharedState = *shared,
									  .Token = token,
									  .Input = input,
									  .Generation = generation};

			if (input.Options.ProjectsOnly)
			{
				for (const std::filesystem::path& file : input.ProjectFiles)
				{
					if (token.IsCancelled())
					{
						break;
					}

					ScanFile(context, file);
				}
			}
			else
			{
				EnumerationInput enumeration;
				enumeration.Root = input.Root;

				// WARNING: Opened on this thread, not shared with the Workspace's, because
				// libgit2 handles are not safe to use from two threads.
				std::optional<Core::Repository> repository;

				if (input.Options.RespectGitIgnore)
				{
					Core::Result<Core::Repository> opened = Core::Repository::Open(input.Root);

					if (opened)
					{
						repository.emplace(std::move(opened.GetValue()));

						enumeration.Ignored = [&repository](const std::filesystem::path& path,
															bool) {
							return repository->IsIgnored(path);
						};
					}
				}

				EnumerateFiles(enumeration, token,
							   [&context](const std::filesystem::path& file) {
								   ScanFile(context, file);
							   });
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
