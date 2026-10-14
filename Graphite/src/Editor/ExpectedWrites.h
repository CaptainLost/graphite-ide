#pragma once

#include "Core/Base/Timestep.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <vector>

namespace Graphite {

	class ExpectedWrites
	{
	public:
		static constexpr float kExpectedWriteSeconds = 2.0f;

		// WARNING: Every path handed in must already be absolute. TakeFor compares whole paths, and a
		// relative one would never match or, resolved against the wrong base, match the wrong file.
		void Expect(const std::vector<std::filesystem::path>& paths);

		bool TakeFor(const std::filesystem::path& path);

		void Tick(Core::Timestep step);

		void Clear() { m_Entries.clear(); }

		std::size_t GetCount() const { return m_Entries.size(); }

	private:
		struct Entry
		{
			std::filesystem::path Path;

			float Age = 0.0f;
		};

		static bool IsExpired(const Entry& entry);

		std::optional<std::size_t> IndexOfEntry(const std::filesystem::path& normalizedPath) const;

		std::vector<Entry> m_Entries;
	};
}
