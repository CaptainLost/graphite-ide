#include "Editor/ExpectedWrites.h"

#include <algorithm>

namespace Graphite {

	void ExpectedWrites::Expect(const std::vector<std::filesystem::path>& paths)
	{
		for (const std::filesystem::path& path : paths)
		{
			// Re-registering a path that is already expected resets its age rather than
			// adding a second entry: two discards of one file in quick succession are one
			// expectation, and two entries would let the second silence a later real change.
			const auto found = std::find_if(m_Entries.begin(), m_Entries.end(),
				[&path](const Entry& entry) { return entry.Path == path; });

			if (found != m_Entries.end())
			{
				found->Age = 0.0f;
				continue;
			}

			m_Entries.push_back({.Path = path, .Age = 0.0f});
		}
	}

	bool ExpectedWrites::TakeFor(const std::filesystem::path& path)
	{
		// Filename equality and a native-path suffix, both required. The suffix alone would
		// let an expectation for "a.txt" claim "xa.txt" - a bare tail match, not a path
		// boundary - and in this feature that means silencing a reload prompt for a file the
		// user never discarded, losing unsaved work.
		const auto found = std::find_if(m_Entries.begin(), m_Entries.end(),
			[&path](const Entry& entry) {
				return path.filename() == entry.Path.filename() &&
					   path.native().ends_with(entry.Path.native());
			});

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
