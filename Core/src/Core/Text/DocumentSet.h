#pragma once

#include "Core/Text/Caret.h"
#include "Core/Text/Columns.h"
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
		std::filesystem::path Path;
		std::optional<TextDocument> Document;
		OpenFailure Failure = OpenFailure::None;
		std::uint64_t Size = 0;
		Caret Caret;
		bool Preview = false;

		std::size_t WidestColumn = 0;

		std::size_t WidestLine = 0;

		RowMap Rows;

		std::size_t WrappedTo = 0;

		std::size_t RowAim = 0;
		std::size_t RowAimAt = NoRowAim;

		// Set by anything that moves the caret from outside the editor's own frame — a jump
		// requested from elsewhere. The editor only scrolls to the caret when it notices the
		// caret changed during its own processing this frame; a change made earlier in the same
		// frame, before the editor's turn, looks like no change at all from inside that
		// processing. This flag says "scroll anyway", and the editor clears it once it has.
		bool PendingScroll = false;

		std::vector<std::size_t> IndentDepths;

		// How many columns a tab character occupies in this document. A layout input like the
		// wrap width beside it, not a preference: Core does not know where the number comes
		// from, only that everything measuring this document must use the same one.
		std::size_t TabWidth = DefaultTabWidth;

		std::string Detail;
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

		std::optional<std::size_t> GetPreviewIndex() const;

		std::string DisplayTitle(std::size_t index) const;

		void RewritePaths(const std::filesystem::path& from, const std::filesystem::path& to);

		void MarkMissing(const std::filesystem::path& path);

		void MarkPresent(const std::filesystem::path& path);

	private:
		OpenOutcome Add(const std::filesystem::path& path, bool permanent, bool confirmed);

		bool HasOtherItemNamed(std::size_t index, const std::string& name) const;

		std::vector<OpenItem> m_Items;
		std::optional<std::size_t> m_Active;
	};
}