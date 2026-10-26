#include "Search/PreviewContent.h"

#include "Core/Text/DocumentSet.h"
#include "Core/Text/FileProbe.h"
#include "Editor/Server/PreviewSemantics.h"
#include "Language/Registry/LanguageRegistry.h"

#include "Core/Base/ByteSize.h"
#include "Core/Base/Result.h"

#include <format>
#include <string>
#include <utility>

namespace Graphite {

	namespace {

		bool AsksBeforeOpening(Core::FileKind kind)
		{
			return kind == Core::FileKind::LargeText || kind == Core::FileKind::TooLarge;
		}

		PreviewText PreviewTextOf(Core::DiskText read)
		{
			switch (read.Outcome)
			{
			case Core::DiskTextOutcome::Read:
				return PreviewText{.State = PreviewState::Ready, .Text = std::move(read.Text)};
			case Core::DiskTextOutcome::Binary:
				return PreviewText{.State = PreviewState::Skipped, .Message = "Binary file."};
			case Core::DiskTextOutcome::TooLarge:
				return PreviewText{.State = PreviewState::Skipped, .Message = "File too large."};
			case Core::DiskTextOutcome::Unreadable:
				return PreviewText{.State = PreviewState::Unreadable, .Message = "Cannot read this file."};
			case Core::DiskTextOutcome::Undecodable:
				return PreviewText{.State = PreviewState::Unreadable, .Message = "Cannot decode this file."};
			}

			return PreviewText{.State = PreviewState::Unreadable, .Message = "Cannot read this file."};
		}
	}

	std::string TooLargeToPreviewText(std::uint64_t fileSize)
	{
		return std::format("File too large to preview ({})", Core::FormatWholeMegabytes(fileSize));
	}

	PreviewText ReadPreviewText(const std::filesystem::path& path)
	{
		const Core::Result<Core::FileProbe> probe = Core::ProbeFile(path);

		if (probe && AsksBeforeOpening(probe.GetValue().Kind))
		{
			return PreviewText{.State = PreviewState::TooLarge, .Message = TooLargeToPreviewText(probe.GetValue().Size)};
		}

		return PreviewTextOf(Core::ReadDiskText(path));
	}

	void PreviewContent::Show(const std::filesystem::path& file, const Core::DocumentSet& documents)
	{
		if (file != m_Path)
		{
			m_Path = file;
			m_Loaded = Core::TextBuffer{};
			m_Buffer = nullptr;
			m_Message.clear();
			m_State = PreviewState::Empty;
			m_CentredOn.reset();

			m_Syntax.Reset();
			m_Syntax.SetSyntax(SyntaxOf(LanguageOfPath(m_Path)));
		}

		if (const Core::TextBuffer* openBuffer = Core::BufferOfOpenDocument(documents, m_Path))
		{
			m_Buffer = openBuffer;
			m_Message.clear();
			m_State = PreviewState::Ready;
			DeclareToServer();
			return;
		}

		const bool alreadyHoldingOwnCopy = m_State == PreviewState::Ready && m_Buffer == &m_Loaded;

		if (alreadyHoldingOwnCopy)
		{
			DeclareToServer();
			return;
		}

		// WARNING: Kept while the same file is shown, rather than probed again on every frame.
		if (m_State == PreviewState::TooLarge)
		{
			return;
		}

		LoadFromDisk();
		DeclareToServer();
	}

	void PreviewContent::Clear()
	{
		m_Path.clear();
		m_Loaded = Core::TextBuffer{};
		m_Buffer = nullptr;
		m_Message.clear();
		m_State = PreviewState::Empty;
		m_CentredOn.reset();

		m_Syntax.Reset();
		m_Syntax.SetSyntax(SyntaxSource{});
	}

	bool PreviewContent::TakeCentring(std::size_t line)
	{
		if (m_CentredOn == line)
		{
			return false;
		}

		m_CentredOn = line;
		return true;
	}

	const std::vector<Token>& PreviewContent::TokensOf(std::size_t line)
	{
		static const std::vector<Token> noTokens;

		if (m_Buffer == nullptr)
		{
			return noTokens;
		}

		return m_Syntax.TokensOf(*m_Buffer, line);
	}

	const std::vector<SemanticSpan>& PreviewContent::SpansOn(std::size_t line) const
	{
		static const std::vector<SemanticSpan> noSpans;

		return m_Semantics == nullptr ? noSpans : m_Semantics->SpansOn(m_Path, line);
	}

	void PreviewContent::SetTabWidth(std::size_t width)
	{
		if (width == 0)
		{
			return;
		}

		m_TabWidth = width;
		m_Syntax.SetTabWidth(width);
	}

	void PreviewContent::DeclareToServer()
	{
		if (m_Semantics == nullptr || m_Buffer == nullptr)
		{
			return;
		}

		const bool openInEditor = m_Buffer != &m_Loaded;

		m_Semantics->Declare(m_Path, *m_Buffer, m_TabWidth, openInEditor);
	}

	void PreviewContent::LoadFromDisk()
	{
		m_Buffer = nullptr;

		PreviewText read = ReadPreviewText(m_Path);

		m_State = read.State;
		m_Message = std::move(read.Message);

		if (read.State == PreviewState::Ready)
		{
			m_Loaded = Core::TextBuffer{std::move(read.Text)};
			m_Buffer = &m_Loaded;
		}
	}
}
