#include "Language/Analysis/AnalysisWalk.h"

#include "Core/Base/Base.h"

#include "Language/Registry/LanguageRegistry.h"
#include "Language/Server/ServerCatalog.h"
#include "Search/FileEnumerator.h"

#include "Core/App/Redraw.h"
#include "Core/Git/Repository.h"

#include <optional>
#include <utility>

namespace Graphite {

	namespace {

		// A file is worth queueing only when its language has a server configured to say
		// anything about it — asking about anything else would just be a wasted round trip.
		bool IsAnalysable(const std::filesystem::path& file)
		{
			return LanguageHasServers(LanguageOfPath(file));
		}

		// Stops the walk at the edge of somebody else's project. A directory carrying its own
		// root marker is where RootFor stops climbing, so every file below it belongs to a
		// different language server than this workspace's — one that would have to be started
		// from nothing, usually with no compile database, to answer about code the reader never
		// opened. In this repository that is the seven submodules under vendor/, which are four
		// fifths of the tree and the overwhelming majority of what the pass used to cost.
		//
		// Rejected at the directory, not at each file below it: a directory answered true is not
		// descended into at all, which is what makes this cheap rather than merely correct.
		//
		// The walk's own root is exempt, and has to be: every repository this runs on carries a
		// marker at its top, so excluding a marked directory without this would switch the
		// feature off everywhere.
		IgnorePredicate SkipNestedProjects(const std::filesystem::path& root)
		{
			return [root](const std::filesystem::path& path, bool isDirectory) { return isDirectory && path != root && IsProjectRoot(path); };
		}
	}

	void CollectAnalysable(const std::filesystem::path& root, const Core::CancellationToken& token,
						   const std::function<void(const std::filesystem::path&)>& onFile)
	{
		EnumerationInput enumeration;
		enumeration.Root = root;
		enumeration.Ignored = SkipNestedProjects(root);

		EnumerateFiles(enumeration, token, [&onFile](const std::filesystem::path& file) {
			if (!IsAnalysable(file))
			{
				return;
			}

			onFile(file);
		});
	}

	AnalysisWalk::~AnalysisWalk()
	{
		m_Token.Cancel();
	}

	void AnalysisWalk::Start(std::filesystem::path root, bool respectGitIgnore)
	{
		Cancel();

		m_Token = Core::CancellationToken{};
		m_Live = true;

		{
			const std::lock_guard<std::mutex> guard(m_Findings->Mutex);
			m_Findings->Paths.clear();
			m_Findings->Walking = true;
		}

		m_Jobs.Submit([findings = m_Findings, token = m_Token, root = std::move(root),
					   respectGitIgnore] {
			EnumerationInput enumeration;
			enumeration.Root = root;

			const IgnorePredicate nested = SkipNestedProjects(root);

			// Opened on this thread rather than shared with the workspace's: libgit2 handles are
			// not safe to use from two threads, which is the same reason SearchService and the
			// warm-up walk each open their own.
			std::optional<Core::Repository> repository;

			if (respectGitIgnore)
			{
				if (Core::Result<Core::Repository> opened = Core::Repository::Open(root))
				{
					repository.emplace(std::move(opened.GetValue()));
				}
			}

			// Both rules, never one: a submodule's contents are not ignored by the parent
			// repository — git treats the submodule as a single entry and says nothing about
			// what is inside it — so gitignore alone would walk straight into vendor/.
			enumeration.Ignored = [&repository, &nested](const std::filesystem::path& path,
														 bool isDirectory) {
				if (nested(path, isDirectory))
				{
					return true;
				}

				return repository && repository->IsIgnored(path);
			};

			EnumerateFiles(enumeration, token, [&findings](const std::filesystem::path& file) {
				if (!IsAnalysable(file))
				{
					return;
				}

				{
					const std::lock_guard<std::mutex> guard(findings->Mutex);
					findings->Paths.push_back(file);
				}

				Core::Redraw::Request();
			});

			{
				const std::lock_guard<std::mutex> guard(findings->Mutex);
				findings->Walking = false;
			}

			Core::Redraw::Request();
		});
	}

	void AnalysisWalk::Cancel()
	{
		if (!m_Live)
		{
			// Nothing was started, or the last thing started has already been cancelled. Both mean
			// the block below would be a fresh allocation replacing an empty one — see the header
			// for why this guard is here and not at the call site.
			return;
		}

		m_Live = false;
		m_Token.Cancel();

		// A fresh block for a fresh walk. The one that was running writes into the old block until
		// it notices the token, and nothing reads that block again.
		m_Findings = Core::CreateRef<Findings>();
	}

	bool AnalysisWalk::IsWalking() const
	{
		const std::lock_guard<std::mutex> guard(m_Findings->Mutex);

		return m_Findings->Walking;
	}

	void AnalysisWalk::Take(std::vector<std::filesystem::path>& out)
	{
		const std::lock_guard<std::mutex> guard(m_Findings->Mutex);

		for (std::filesystem::path& path : m_Findings->Paths)
		{
			out.push_back(std::move(path));
		}

		m_Findings->Paths.clear();
	}
}
