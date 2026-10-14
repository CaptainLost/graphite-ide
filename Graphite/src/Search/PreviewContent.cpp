#include "Search/PreviewContent.h"

#include "Core/Text/DocumentSet.h"
#include "Core/Text/FileProbe.h"
#include "Editor/Server/PreviewSemantics.h"
#include "Language/Registry/LanguageRegistry.h"

#include <string>
#include <utility>

namespace Graphite {

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

		Core::DiskText read = Core::ReadDiskText(m_Path);

		switch (read.Outcome)
		{
		case Core::DiskTextOutcome::Read:
			m_Loaded = Core::TextBuffer{std::move(read.Text)};
			m_Buffer = &m_Loaded;
			m_State = PreviewState::Ready;
			m_Message.clear();
			return;
		case Core::DiskTextOutcome::Binary:
			m_State = PreviewState::Skipped;
			m_Message = "Binary file.";
			return;
		case Core::DiskTextOutcome::TooLarge:
			m_State = PreviewState::Skipped;
			m_Message = "File too large.";
			return;
		case Core::DiskTextOutcome::Unreadable:
			m_State = PreviewState::Unreadable;
			m_Message = "Cannot read this file.";
			return;
		case Core::DiskTextOutcome::Undecodable:
			m_State = PreviewState::Unreadable;
			m_Message = "Cannot decode this file.";
			return;
		}
	}
}
