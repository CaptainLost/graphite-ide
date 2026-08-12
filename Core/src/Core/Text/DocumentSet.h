#pragma once

#include "Core/Text/Caret.h"
#include "Core/Text/RowMap.h"
#include "Core/Text/TextDocument.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Core {

	enum class OpenFailure
	{
		None,
		Binary,
		TooLarge,
		Missing,
		ReadFailed
	};

	inline constexpr std::size_t NoRowAim = static_cast<std::size_t>(-1);

	struct OpenItem
	{
		std::filesystem::path path;
		std::optional<TextDocument> document;
		OpenFailure failure = OpenFailure::None;
		std::uint64_t size = 0;
		Caret caret;
		bool preview = false;

		std::size_t widestColumn = 0;

		std::size_t widestLine = 0;

		RowMap rows;

		std::size_t wrappedTo = 0;

		std::size_t rowAim = 0;
		std::size_t rowAimAt = NoRowAim;

		std::vector<std::size_t> indentDepths;

		std::string detail;
	};

	enum class ItemLoad
	{
		Loaded,

		NeedsConfirmation,

		Failed
	};

	ItemLoad LoadFromFile(OpenItem& item, bool confirmed);

	enum class OpenOutcome
	{
		Opened,
		Activated,
		NeedsConfirmation,
		Failed
	};

	std::string FailureMessage(const OpenItem& item);

	class DocumentSet
	{
	public:
		OpenOutcome Open(const std::filesystem::path& path, bool permanent);

		OpenOutcome OpenConfirmed(const std::filesystem::path& path, bool permanent);

		void Close(std::size_t index);
		void CloseAll();

		std::optional<std::size_t> Find(const std::filesystem::path& path) const;
		std::size_t GetCount() const { return m_Items.size(); }

		OpenItem& At(std::size_t index) { return m_Items[index]; }
		const OpenItem& At(std::size_t index) const { return m_Items[index]; }

		void Activate(std::size_t index);
		std::optional<std::size_t> GetActive() const { return m_Active; }

		void Promote(std::size_t index);

		std::optional<std::size_t> GetPreview() const;

		std::string DisplayTitle(std::size_t index) const;

		void RewritePaths(const std::filesystem::path& from, const std::filesystem::path& to);

		void MarkMissing(const std::filesystem::path& path);

		void MarkPresent(const std::filesystem::path& path);

	private:
		OpenOutcome Add(const std::filesystem::path& path, bool permanent, bool confirmed);

		std::vector<OpenItem> m_Items;
		std::optional<std::size_t> m_Active;
	};
}