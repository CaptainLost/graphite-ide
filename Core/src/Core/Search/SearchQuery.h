#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>

namespace Core {

	struct SearchQuery
	{
		std::string Needle;

		std::string Mask;

		bool MatchCase = false;
		bool WholeWord = false;
	};

	struct SearchOptions
	{
		bool RespectGitIgnore = true;
		bool ProjectsOnly = false;
	};

	struct SearchHit
	{
		std::filesystem::path File;

		std::size_t Line = 0;

		std::string Text;

		std::size_t From = 0;
		std::size_t To = 0;

		struct UnreadSpan
		{
			std::size_t Column = 0;
			std::size_t EndColumn = 0;
		};

		std::optional<UnreadSpan> Unread;
	};
}
