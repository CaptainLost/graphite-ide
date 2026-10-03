#include "Editor/ExpectedWrites.h"

#include "Core/FileSystem/FileSystem.h"

#include <algorithm>

namespace Graphite {

	void ExpectedWrites::Expect(const std::vector<std::filesystem::path>& paths)
	{
		for (const std::filesystem::path& path : paths)
		{
			const std::filesystem::path normalized = Core::NormalizePath(path);

			// Re-registering a path that is already expected resets its age rather than
			// adding a second entry: two discards of one file in quick succession are one
			// expectation, and two entries would let the second silence a later real change.
			// The same comparison TakeFor uses, so the two never disagree about what counts
			// as "the same path".
			const auto found = std::find_if(m_Entries.begin(), m_Entries.end(),
											[&normalized](const Entry& entry) { return Core::SamePath(entry.Path, normalized); });

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
		const std::filesystem::path normalized = Core::NormalizePath(path);

		const auto found = std::find_if(m_Entries.begin(), m_Entries.end(),
										[&normalized](const Entry& entry) { return Core::SamePath(entry.Path, normalized); });

		if (found == m_Entries.end())
		{
			return false;
		}

		m_Entries.erase(found);
		return true;
	}

	void ExpectedWrites::Tick(Core::Timestep step)
	{
		const float seconds = step.GetSeconds();

		for (Entry& entry : m_Entries)
		{
			entry.Age += seconds;
		}

		std::erase_if(m_Entries,
					  [](const Entry& entry) { return entry.Age >= kExpectedWriteSeconds; });
	}
}
