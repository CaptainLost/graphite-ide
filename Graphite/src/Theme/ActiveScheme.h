#pragma once

#include "Theme/BuiltInSchemes.h"
#include "Theme/ColorScheme.h"

#include <cstdint>
#include <filesystem>
#include <string_view>

namespace Graphite {

	class AppSettings;

	ResolvedScheme ResolveNamed(std::string_view name, const std::filesystem::path& directory);

	class ActiveScheme
	{
	public:
		void Load(const AppSettings& settings);

		const ResolvedScheme& Get() const { return m_Resolved; }

		void Set(const ResolvedScheme& resolved)
		{
			m_Resolved = resolved;
			++m_Version;
		}

		std::uint32_t GetVersion() const { return m_Version; }

	private:
		ResolvedScheme m_Resolved = DarkScheme();

		std::uint32_t m_Version = 1;
	};
}
