#include "Editor/ExpectedWrites.h"

#include "Core/Fs/FileSystem.h"

#include <algorithm>

namespace Graphite {

	namespace {

		// lexically_normal collapses "a/../b" into "b" without touching the filesystem -
		// exactly what an entry that may outlive the file it names needs, since equivalent()
		// would fail for a path a later delete has already removed. make_preferred converts
		// git's forward slashes and the query side's native separators to the same spelling,
		// so the two are comparable byte for byte.
		std::filesystem::path Normalize(const std::filesystem::path& path)
		{
			std::filesystem::path normalized = path.lexically_normal();
			normalized.make_preferred();
			return normalized;
		}

		// Whole-path equality, case-folded: Windows paths are case-insensitive, and this is
		// the same comparison the rest of the codebase already uses for names (FileTree,
		// CsprojReader, VcxprojReader, SolutionReader all sort and match through it).
		bool SamePath(const std::filesystem::path& left, const std::filesystem::path& right)
		{
			return Core::CompareNames(Core::ToUtf8(left), Core::ToUtf8(right)) == 0;
		}
	}

	void ExpectedWrites::Expect(const std::vector<std::filesystem::path>& paths)
	{
		for (const std::filesystem::path& path : paths)
		{
			const std::filesystem::path normalized = Normalize(path);

			// Re-registering a path that is already expected resets its age rather than
			// adding a second entry: two discards of one file in quick succession are one
			// expectation, and two entries would let the second silence a later real change.
			// The same comparison TakeFor uses, so the two never disagree about what counts
			// as "the same path".
			const auto found = std::find_if(m_Entries.begin(), m_Entries.end(),
				[&normalized](const Entry& entry) { return SamePath(entry.Path, normalized); });

			if (found != m_Entries.end())
			{
				found->Age = 0.0f;
				continue;
			}

			m_Entries.push_back({.Path = normalized, .Age = 0.0f});
		}
	}

	bool ExpectedWrites::TakeFor(const std::filesystem::path& path)
	{
		const std::filesystem::path normalized = Normalize(path);

		const auto found = std::find_if(m_Entries.begin(), m_Entries.end(),
			[&normalized](const Entry& entry) { return SamePath(entry.Path, normalized); });

		if (found == m_Entries.end())
		{
			return false;
		}

		m_Entries.erase(found);
		return true;
	}

	void ExpectedWrites::Tick(float seconds)
	{
		for (Entry& entry : m_Entries)
		{
			entry.Age += seconds;
		}

		std::erase_if(m_Entries,
			[](const Entry& entry) { return entry.Age >= kExpectedWriteSeconds; });
	}
}
