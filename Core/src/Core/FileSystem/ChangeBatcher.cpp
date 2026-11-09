#include "corepch.h"

#include "Core/FileSystem/ChangeBatcher.h"

#include <iterator>
#include <utility>

namespace Core {

	namespace {

		constexpr const char* kIgnoreRulesFileName = ".gitignore";

		bool IsRepositoryStateFile(const std::filesystem::path& fileName)
		{
			return fileName == "HEAD" || fileName == "index" || fileName == "packed-refs";
		}

		bool IsInsideGitDirectory(const std::filesystem::path& pathRelativeToRoot)
		{
			for (const std::filesystem::path& enclosingDirectory : pathRelativeToRoot.parent_path())
			{
				if (enclosingDirectory == ".git")
				{
					return true;
				}
			}

			return false;
		}

		bool IsGitExcludeFile(const std::filesystem::path& pathRelativeToRoot)
		{
			return pathRelativeToRoot == std::filesystem::path{".git"} / "info" / "exclude";
		}

		bool IsGitConfigFile(const std::filesystem::path& pathRelativeToRoot)
		{
			return pathRelativeToRoot == std::filesystem::path{".git"} / "config";
		}

		bool AppliesToTheWholeTree(const std::filesystem::path& pathRelativeToRoot)
		{
			return IsGitExcludeFile(pathRelativeToRoot) || IsGitConfigFile(pathRelativeToRoot);
		}
	}

	bool NamesRepositoryState(const std::filesystem::path& pathRelativeToRoot)
	{
		std::filesystem::path::iterator component = pathRelativeToRoot.begin();

		if (component == pathRelativeToRoot.end() || *component != ".git")
		{
			return false;
		}

		++component;

		if (component == pathRelativeToRoot.end())
		{
			return false;
		}

		const bool isUnderRefs = *component == "refs";

		if (isUnderRefs)
		{
			return true;
		}

		const bool isDirectlyInsideGitDirectory = std::next(component) == pathRelativeToRoot.end();

		if (!isDirectlyInsideGitDirectory)
		{
			return false;
		}

		return IsRepositoryStateFile(*component);
	}

	bool NamesIgnoreRules(const std::filesystem::path& pathRelativeToRoot)
	{
		const bool isAFolderRulesFile =
			pathRelativeToRoot.filename() == kIgnoreRulesFileName && !IsInsideGitDirectory(pathRelativeToRoot);

		return isAFolderRulesFile || IsGitExcludeFile(pathRelativeToRoot) || IsGitConfigFile(pathRelativeToRoot);
	}

	ChangeBatcher::ChangeBatcher(std::filesystem::path root) : m_Root(std::move(root)) {}

	void ChangeBatcher::Record(const std::filesystem::path& changedPath)
	{
		const std::filesystem::path pathRelativeToRoot = changedPath.lexically_relative(m_Root);
		const ChangeCollector::Clock::time_point now = ChangeCollector::Clock::now();

		if (NamesIgnoreRules(pathRelativeToRoot))
		{
			m_Collector.AddIgnoreRulesChange(now);
		}

		if (AppliesToTheWholeTree(pathRelativeToRoot))
		{
			m_Collector.Add(m_Root, now);
			return;
		}

		if (NamesRepositoryState(pathRelativeToRoot))
		{
			m_RepositoryStateChanged = true;
			return;
		}

		if (IsInsideGitDirectory(pathRelativeToRoot))
		{
			return;
		}

		std::filesystem::path directoryHoldingTheEntry = changedPath.parent_path();
		directoryHoldingTheEntry.make_preferred();

		m_Collector.Add(directoryHoldingTheEntry, now);
	}

	void ChangeBatcher::RecordOverflow()
	{
		m_Collector.AddOverflow(ChangeCollector::Clock::now());
	}

	void ChangeBatcher::Respell(std::filesystem::path sameRoot)
	{
		m_Root = std::move(sameRoot);
	}

	void ChangeBatcher::Rebase(std::filesystem::path newRoot)
	{
		m_Root = std::move(newRoot);
		m_Collector = ChangeCollector{};
	}

	ChangeBatch ChangeBatcher::TakeSettled(std::chrono::milliseconds quietPeriod)
	{
		std::vector<std::filesystem::path> settledDirectories = m_Collector.TakeSettled(ChangeCollector::Clock::now(), quietPeriod);

		// HasOverflowed describes the batch the most recent TakeSettled handed over, so it is read after that call.
		return ChangeBatch{
			.Directories = std::move(settledDirectories),
			.Overflowed = m_Collector.HasOverflowed(),
			.IgnoreRulesChanged = m_Collector.HasIgnoreRulesChanged(),
			.RepositoryStateChanged = std::exchange(m_RepositoryStateChanged, false),
		};
	}

	std::optional<SteadyClock::time_point> ChangeBatcher::SettlesAt(std::chrono::milliseconds quietPeriod) const
	{
		return m_Collector.SettlesAt(quietPeriod);
	}
}
