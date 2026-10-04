#include "Git/RelativePaths.h"

#include "Core/FileSystem/FileSystem.h"

#include <cctype>
#include <cstddef>

namespace Graphite {

	namespace {
		bool IsSeparator(char c)
		{
			return c == '/' || c == '\\';
		}

		bool IsWhitespace(char c)
		{
			return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
		}

		// What can follow a path in a message but never begins a name in one.
		bool EndsPath(char c)
		{
			return IsWhitespace(c) || std::string_view("'\"()[]<>,;:").find(c) != std::string_view::npos;
		}

		// Where a path may start: the start of a word, or just inside a quote or a bracket.
		bool StartsToken(std::string_view message, std::size_t at)
		{
			if (at == 0)
			{
				return true;
			}

			const char before = message[at - 1];
			return IsWhitespace(before) || before == '\'' || before == '"' || before == '(' || before == '[' ||
				   before == '<';
		}

		bool SameChar(char a, char b, std::size_t index, std::string_view root)
		{
			if (IsSeparator(a) && IsSeparator(b))
			{
				return true;
			}

			// The drive letter alone matches in either case: Windows hands it out either way,
			// while the rest of the path comes from the same place in the same case.
			const bool driveLetter = index == 0 && root.size() >= 2 && root[1] == ':';

			if (driveLetter)
			{
				return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
			}

			return a == b;
		}

		// How long the prefix `root` plus one separator is at `at`, or 0 when the message
		// does not start a path inside the root there.
		std::size_t RootPrefixAt(std::string_view message, std::size_t at, std::string_view root)
		{
			if (message.size() - at < root.size() + 2)
			{
				return 0;
			}

			for (std::size_t i = 0; i < root.size(); ++i)
			{
				if (!SameChar(message[at + i], root[i], i, root))
				{
					return 0;
				}
			}

			const std::size_t separator = at + root.size();

			// Strictly inside: a separator, then a name rather than the end of the path.
			if (!IsSeparator(message[separator]) || EndsPath(message[separator + 1]))
			{
				return 0;
			}

			return root.size() + 1;
		}
	}

	std::string RelativiseUnder(std::string_view message, const std::filesystem::path& root)
	{
		std::string rootText = Core::ToUtf8(root);

		while (!rootText.empty() && IsSeparator(rootText.back()))
		{
			rootText.pop_back();
		}

		// Checked once the separators are gone: a root of "/" alone would otherwise match
		// every absolute path in the message, inside the repository or not.
		if (rootText.empty())
		{
			return std::string(message);
		}

		std::string result;
		result.reserve(message.size());

		std::size_t i = 0;

		while (i < message.size())
		{
			if (StartsToken(message, i))
			{
				if (const std::size_t prefix = RootPrefixAt(message, i, rootText); prefix > 0)
				{
					i += prefix;
					continue;
				}
			}

			result += message[i];
			++i;
		}

		return result;
	}
}
