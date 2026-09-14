#include "Lang/Analysis/AnalysisWalk.h"

#include "Lang/Registry/LanguageRegistry.h"
#include "Lang/Server/ServerRegistry.h"
#include "Search/FileEnumerator.h"

#include "Core/Git/Repository.h"

#include <optional>
#include <utility>

namespace Graphite {

	void CollectAnalysable(const std::filesystem::path& root, const Core::CancellationToken& token,
						   const std::function<void(const std::filesystem::path&)>& onFile)
	{
		EnumerationInput enumeration;
		enumeration.Root = root;

		EnumerateFiles(enumeration, token, [&onFile](const std::filesystem::path& file)
		{
			if (ServerConfigFor(LanguageOfPath(file)) == nullptr)
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

		{
			const std::lock_guard<std::mutex> guard(m_Findings->Mutex);
			m_Findings->Paths.clear();
			m_Findings->Walking = true;
		}

		m_Jobs.Submit([findings = m_Findings, token = m_Token, root = std::move(root),
					   respectGitIgnore]
		{
			EnumerationInput enumeration;
			enumeration.Root = root;

			// Opened on this thread rather than shared with the workspace's: libgit2 handles are
			// not safe to use from two threads, which is the same reason SearchService and the
			// warm-up walk each open their own.
			std::optional<Core::Repository> repository;

			if (respectGitIgnore)
			{
				if (Core::Result<Core::Repository> opened = Core::Repository::Open(root))
				{
					repository.emplace(std::move(opened.GetValue()));

					enumeration.Ignored = [&repository](const std::filesystem::path& path, bool)
					{ return repository->IsIgnored(path); };
				}
			}

			EnumerateFiles(enumeration, token, [&findings](const std::filesystem::path& file)
			{
				if (ServerConfigFor(LanguageOfPath(file)) == nullptr)
				{
					return;
				}

				const std::lock_guard<std::mutex> guard(findings->Mutex);
				findings->Paths.push_back(file);
			});

			const std::lock_guard<std::mutex> guard(findings->Mutex);
			findings->Walking = false;
		});
	}

	void AnalysisWalk::Cancel()
	{
		m_Token.Cancel();

		// A fresh block for a fresh walk. The one that was running writes into the old block until
		// it notices the token, and nothing reads that block again.
		m_Findings = std::make_shared<Findings>();
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
