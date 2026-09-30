#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace Graphite {

	// The folders the user has said they trust. A folder is trusted when it is listed or lies
	// inside a listed one. Anything else opens in restricted mode: whatever would run code or
	// read configuration the folder controls - the language server, the background analysis,
	// and later the build and the debugger - stays off there (ADR 0532).
	//
	// Paths are compared as Windows compares them: case-insensitively, whichever way the
	// slashes lean, with or without a trailing separator. Entries are kept absolute and
	// normalised, in the form they are shown in.
	class WorkspaceTrust
	{
	public:
		static constexpr int kFileVersion = 1;

		// Reads `file` if it exists. No file trusts nothing; an unreadable one trusts nothing
		// too, and says so in the log; one written by a newer version is read but never
		// written back. An empty path keeps the list in memory, which is what tests use.
		explicit WorkspaceTrust(std::filesystem::path file = {});

		bool IsTrusted(const std::filesystem::path& folder) const;

		// The entry that trusts `folder` - itself or a folder above it - or nothing.
		std::optional<std::filesystem::path> TrustedThrough(const std::filesystem::path& folder) const;

		// Whether `folder` is itself an entry, rather than trusted through one above it.
		bool IsEntry(const std::filesystem::path& folder) const;

		// Adds `folder` and writes the file. A relative or empty path, or one already listed,
		// changes nothing.
		void Trust(const std::filesystem::path& folder);

		// Removes the entry naming `entry`, however it is spelled, and writes the file.
		void Revoke(const std::filesystem::path& entry);

		const std::vector<std::filesystem::path>& Entries() const { return m_Entries; }

		// Moves on every change, so a reader can notice one with a single compare per frame.
		std::uint64_t Revision() const { return m_Revision; }

	private:
		void Save() const;

		std::filesystem::path m_File;
		std::vector<std::filesystem::path> m_Entries;
		std::uint64_t m_Revision = 0;
		bool m_Writable = true;
	};

	// The folder a "trust everything in here" button would offer for `folder`: its parent,
	// unless that is a drive root - one click should not trust a whole drive.
	std::optional<std::filesystem::path> ParentWorthTrusting(const std::filesystem::path& folder);
}
