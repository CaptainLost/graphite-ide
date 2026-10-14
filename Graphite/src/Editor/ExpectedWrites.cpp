#include "Editor/ExpectedWrites.h"

#include "Core/FileSystem/FileSystem.h"

#include <cstddef>
#include <optional>

namespace Graphite {
	void ExpectedWrites::Expect(const std::vector<std::filesystem::path>& paths)
	{
		for (const std::filesystem::path& path : paths)
		{
			const std::filesystem::path normalized = Core::NormalizePath(path);

			const std::optional<std::size_t> existingIndex = IndexOfEntry(normalized);

			if (existingIndex)
			{
				m_Entries[*existingIndex].Age = 0.0f;
				continue;
			}

			m_Entries.push_back({.Path = normalized, .Age = 0.0f});
		}
	}

	bool ExpectedWrites::TakeFor(const std::filesystem::path& path)
	{
		const std::filesystem::path normalized = Core::NormalizePath(path);

		const std::optional<std::size_t> index = IndexOfEntry(normalized);

		if (!index)
		{
			return false;
		}

		m_Entries.erase(m_Entries.begin() + static_cast<std::ptrdiff_t>(*index));
		return true;
	}

	void ExpectedWrites::Tick(Core::Timestep step)
	{
		const float seconds = step.GetSeconds();

		for (Entry& entry : m_Entries)
		{
			entry.Age += seconds;
		}

		std::erase_if(m_Entries, IsExpired);
	}

	bool ExpectedWrites::IsExpired(const Entry& entry)
	{
		return entry.Age >= kExpectedWriteSeconds;
	}

	std::optional<std::size_t>
	ExpectedWrites::IndexOfEntry(const std::filesystem::path& normalizedPath) const
	{
		for (std::size_t index = 0; index < m_Entries.size(); ++index)
		{
			if (Core::SamePath(m_Entries[index].Path, normalizedPath))
			{
				return index;
			}
		}

		return std::nullopt;
	}
}
