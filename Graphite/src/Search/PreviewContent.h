#pragma once

#include "Core/Text/Columns.h"
#include "Core/Text/TextBuffer.h"
#include "Editor/Overlays/SemanticOverlay.h"
#include "Language/Syntax/SyntaxCache.h"
#include "Language/Syntax/Token.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Core {

	class DocumentSet;
}

namespace Graphite {

	class PreviewSemantics;

	enum class PreviewState
	{
		Empty,
		Ready,
		Unreadable,
		Skipped,
		TooLarge,
	};

	struct PreviewText
	{
		PreviewState State = PreviewState::Unreadable;
		std::string Text;
		std::string Message;
	};

	PreviewText ReadPreviewText(const std::filesystem::path& path);

	std::string TooLargeToPreviewText(std::uint64_t fileSize);

	class PreviewContent
	{
	public:
		explicit PreviewContent(PreviewSemantics* semantics = nullptr) : m_Semantics(semantics) {}

		void Show(const std::filesystem::path& file, const Core::DocumentSet& documents);

		void Clear();

		PreviewState GetState() const { return m_State; }

		const std::filesystem::path& GetPath() const { return m_Path; }

		// WARNING: Not named GetMessage, because <windows.h> defines that as a macro for GetMessageW.
		const std::string& GetDetail() const { return m_Message; }

		// WARNING: The pointer may point into Core::DocumentSet, which reallocates as tabs open and close,
		// so it is only valid until the set next changes.
		const Core::TextBuffer* GetBuffer() const { return m_Buffer; }

		const std::vector<Token>& TokensOf(std::size_t line);

		const std::vector<SemanticSpan>& SpansOn(std::size_t line) const;

		void SetTabWidth(std::size_t width);

		bool TakeCentring(std::size_t line);

	private:
		void LoadFromDisk();
		void DeclareToServer();

		std::filesystem::path m_Path;
		PreviewState m_State = PreviewState::Empty;
		std::string m_Message;

		Core::TextBuffer m_Loaded;

		const Core::TextBuffer* m_Buffer = nullptr;

		SyntaxCache m_Syntax;

		PreviewSemantics* m_Semantics = nullptr;

		std::size_t m_TabWidth = Core::DefaultTabWidth;

		std::optional<std::size_t> m_CentredOn;
	};
}
