#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	// What the command line asks of this instance: `Graphite.exe [--user-data <dir>] [<folder>]`.
	struct CommandLine
	{
		// Where every user file of this instance lives instead of the user's profile, so a second
		// instance can run beside the user's own without sharing its settings, layout or logs.
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
