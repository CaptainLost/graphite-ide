#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	// What the command line asks of this instance: `Graphite.exe [--user-data <dir>] [<folder>]`.
	struct CommandLine
	{
		// Where this instance keeps its user files instead of the user's profile - the settings,
		// the window layout, the themes, the sessions cache, the logs and the list of trusted
		// folders - so a second instance can run beside the user's own without sharing any of
		// them. What a folder keeps of itself under <folder>/.graphite - its open tabs and
		// terminals, its analysis cache - is not redirected: two instances with one folder open
		// still share it.
		std::optional<std::filesystem::path> UserData;

		// The folder to open at start; empty when none was named.
		std::filesystem::path Folder;
	};

	// Reads the arguments after the program name. "--user-data" takes the argument that follows
	// it; with none following, the option is ignored (the caller logs that) rather than leaving
	// it to take the folder in its place. The first argument that is not an option is the folder;
	// any after it are ignored. Wide, so a folder with characters outside the ANSI code page
	// arrives intact.
	CommandLine ParseCommandLine(const std::vector<std::wstring>& arguments);

	// The process's own arguments, without the program name. Empty where there is no such
	// reading for the platform.
	std::vector<std::wstring> ReadProcessCommandLine();
}
