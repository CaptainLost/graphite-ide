#include "Language/Analysis/AnalysisWalk.h"

#include "Core/Base/Base.h"

#include "Language/Registry/LanguageRegistry.h"
#include "Language/Server/ServerCatalog.h"
#include "Language/Server/SolutionScope.h"
#include "Search/FileEnumerator.h"

#include "Core/App/Redraw.h"
#include "Core/Git/Repository.h"

#include <optional>
#include <utility>

namespace Graphite {

	namespace {

		bool BelongsToWalk(const std::filesystem::path& file, SolutionScope& solutionScope)
		{
			const LanguageId language = LanguageOfPath(file);

			return LanguageHasServers(language) && (!LanguageRootsAtSolution(language) || solutionScope.Includes(file));
		}

		// WARNING: The walk's own root is exempt from the nested-project test. Every repository
		// carries a marker at its top, so without the exemption the feature would be off everywhere.
		IgnorePredicate SkipNestedProjects(const std::filesystem::path& root)
		{
			return [root](const std::filesystem::path& path, bool isDirectory) { return isDirectory && path != root && IsProjectRoot(path); };
		}
	}

	void CollectAnalysable(const std::filesystem::path& root, const std::filesystem::path& selectedSolution,
						   const Core::CancellationToken& token, const std::function<void(const std::filesystem::path&)>& onFile)
	{
		EnumerationInput enumeration;
		enumeration.Root = root;
		enumeration.Ignored = SkipNestedProjects(root);

		SolutionScope solutionScope{root, selectedSolution};

		EnumerateFiles(enumeration, token, [&onFile, &solutionScope](const std::filesystem::path& file) {
			if (!BelongsToWalk(file, solutionScope))
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

	void AnalysisWalk::Start(std::filesystem::path root, std::filesystem::path selectedSolution, bool respectGitIgnore)
	{
		Cancel();

		m_Token = Core::CancellationToken{};
		m_HasUncancelledWalk = true;

		{
			const std::lock_guard<std::mutex> lock(m_Findings->Mutex);
			m_Findings->Paths.clear();
			m_Findings->Walking = true;
		}

		m_Jobs.Submit([findings = m_Findings, token = m_Token, root = std::move(root),
					   selectedSolution = std::move(selectedSolution), respectGitIgnore] {
			EnumerationInput enumeration;
			enumeration.Root = root;

			const IgnorePredicate isNestedProject = SkipNestedProjects(root);

			// WARNING: Opened on this thread, never shared with the workspace's repository:
			// libgit2 handles are not safe to use from two threads.
			std::optional<Core::Repository> repository;

			if (respectGitIgnore)
			{
				if (Core::Result<Core::Repository> opened = Core::Repository::Open(root))
				{
					repository.emplace(std::move(opened.GetValue()));
				}
			}

			// WARNING: Both rules are needed. Git treats a submodule as a single entry and says
			// nothing about what is inside it, so gitignore alone would walk into the submodule.
			enumeration.Ignored = [&repository, &isNestedProject](const std::filesystem::path& path,
																  bool isDirectory) {
				if (isNestedProject(path, isDirectory))
				{
					return true;
				}

				return repository && repository->IsIgnored(path);
			};

			SolutionScope solutionScope{root, selectedSolution};

			EnumerateFiles(enumeration, token, [&findings, &solutionScope](const std::filesystem::path& file) {
				if (!BelongsToWalk(file, solutionScope))
				{
					return;
				}

				{
					const std::lock_guard<std::mutex> lock(findings->Mutex);
					findings->Paths.push_back(file);
				}

				Core::Redraw::Request();
			});

			{
				const std::lock_guard<std::mutex> lock(findings->Mutex);
				findings->Walking = false;
			}

			Core::Redraw::Request();
		});
	}

	void AnalysisWalk::Cancel()
	{
		// WARNING: Called every frame while the feature is switched off, so the early return must
		// stay here: below it allocates a fresh findings block.
		const bool nothingToCancel = !m_HasUncancelledWalk;

		if (nothingToCancel)
		{
			return;
		}

		m_HasUncancelledWalk = false;
		m_Token.Cancel();

		m_Findings = Core::CreateRef<Findings>();
	}

	bool AnalysisWalk::IsWalking() const
	{
		const std::lock_guard<std::mutex> lock(m_Findings->Mutex);

		return m_Findings->Walking;
	}

	void AnalysisWalk::Take(std::vector<std::filesystem::path>& destination)
	{
		const std::lock_guard<std::mutex> lock(m_Findings->Mutex);

		for (std::filesystem::path& path : m_Findings->Paths)
		{
			destination.push_back(std::move(path));
		}

		m_Findings->Paths.clear();
	}
}
