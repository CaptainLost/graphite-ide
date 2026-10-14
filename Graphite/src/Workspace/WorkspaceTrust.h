#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace Graphite {

	class WorkspaceTrust
	{
	public:
		static constexpr int kFileVersion = 1;

		explicit WorkspaceTrust(std::filesystem::path file = {});

		bool IsTrusted(const std::filesystem::path& folder) const;

		std::optional<std::filesystem::path> TrustedThrough(const std::filesystem::path& folder) const;

		bool IsEntry(const std::filesystem::path& folder) const;

		void Trust(const std::filesystem::path& folder);

		void Revoke(const std::filesystem::path& entry);

		const std::vector<std::filesystem::path>& GetEntries() const { return m_Entries; }

		std::uint64_t GetRevision() const { return m_Revision; }

	private:
		void Save() const;

		std::filesystem::path m_File;
		std::vector<std::filesystem::path> m_Entries;
		std::uint64_t m_Revision = 0;
		bool m_Writable = true;
	};

	std::optional<std::filesystem::path> ParentWorthTrusting(const std::filesystem::path& folder);
}
