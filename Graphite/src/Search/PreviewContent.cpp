#include "Search/PreviewContent.h"

#include "Core/Text/Codec.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Text/FileProbe.h"
#include "Editor/PreviewSemantics.h"
#include "Lang/Registry/LanguageRegistry.h"

#include <fstream>
#include <iterator>
#include <optional>
#include <string>

namespace Graphite {

	namespace {

		// The same read the search itself does, deliberately: a preview that decoded a file
		// differently from the search that found the hit would disagree with the row above it.
		std::optional<std::string> ReadAll(const std::filesystem::path& path)
		{
			std::ifstream in(path, std::ios::binary);

			if (!in)
			{
				return std::nullopt;
			}

			return std::string(std::istreambuf_iterator<char>(in),
							   std::istreambuf_iterator<char>());
		}
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

			// One file's colours at a time. The cache's entry states belong to the file that
			// was showing — line 40 of the next file starts in whatever state its own line 39
			// left, not in the one carried over from a block comment in the last file.
			m_Syntax.Reset();
			m_Syntax.SetLexer(LexerFor(LanguageOfPath(m_Path)));
		}

		// Re-resolved on every call rather than remembered: the set reallocates as tabs open
		// and close, so a pointer kept from last frame may not point at a document any more —
		// and a file opened since the last frame has to start winning over the copy on disk.
		if (const std::optional<std::size_t> index = documents.Find(m_Path))
		{
			const Core::OpenItem& item = documents.At(*index);

			if (item.Document)
			{
				m_Buffer = &item.Document->GetBuffer();
				m_Message.clear();
				m_State = PreviewState::Ready;
				DeclareToServer();
				return;
			}
		}

		// Already have our own copy of this file and nothing has opened it since. The path did
		// not change, so the copy is still of the right file.
		if (m_State == PreviewState::Ready && m_Buffer == &m_Loaded)
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

		m_Syntax.Reset();
		m_Syntax.SetLexer(nullptr);
	}

	const std::vector<Token>& PreviewContent::TokensOf(std::size_t line)
	{
		// Function-local rather than a member: a caller holding this reference past the next
		// call is holding one into the cache either way, and a shared empty vector cannot be
		// mistaken for one file's tokens.
		static const std::vector<Token> none;

		if (m_Buffer == nullptr)
		{
			return none;
		}

		return m_Syntax.TokensOf(*m_Buffer, line);
	}

	const std::vector<SemanticSpan>& PreviewContent::SpansOn(std::size_t line) const
	{
		static const std::vector<SemanticSpan> none;

		return m_Semantics == nullptr ? none : m_Semantics->SpansOn(m_Path, line);
	}

	void PreviewContent::SetTabWidth(std::size_t width)
	{
		if (width != 0)
		{
			m_TabWidth = width;
		}

		// SyntaxCache::SetTabWidth already ignores a width it is holding, so this costs nothing
		// to call every frame.
		m_Syntax.SetTabWidth(width);
	}

	void PreviewContent::DeclareToServer()
	{
		if (m_Semantics == nullptr || m_Buffer == nullptr)
		{
			return;
		}

		// A buffer that is not our own copy came from a document with a tab, which means the
		// editor already told the server about it. That is the whole difference the mediator
		// needs, and it is a pointer comparison rather than a second search of the document set.
		m_Semantics->Declare(m_Path, *m_Buffer, m_TabWidth, m_Buffer != &m_Loaded);
	}

	void PreviewContent::LoadFromDisk()
	{
		m_Buffer = nullptr;

		const Core::Result<Core::FileProbe> probed = Core::ProbeFile(m_Path);

		if (!probed)
		{
			m_State = PreviewState::Unreadable;
			m_Message = "Cannot read this file.";
			return;
		}

		const Core::FileProbe& probe = probed.GetValue();

		if (probe.Kind == Core::FileKind::Binary || probe.Kind == Core::FileKind::TooLarge)
		{
			m_State = PreviewState::Skipped;
			m_Message = probe.Kind == Core::FileKind::Binary ? "Binary file." : "File too large.";
			return;
		}

		const std::optional<std::string> bytes = ReadAll(m_Path);

		if (!bytes)
		{
			m_State = PreviewState::Unreadable;
			m_Message = "Cannot read this file.";
			return;
		}

		const Core::Result<std::string> text = Core::Decode(*bytes, probe.FileEncoding);

		if (!text)
		{
			m_State = PreviewState::Unreadable;
			m_Message = "Cannot decode this file.";
			return;
		}

		m_Loaded = Core::TextBuffer{text.GetValue()};
		m_Buffer = &m_Loaded;
		m_State = PreviewState::Ready;
		m_Message.clear();
	}
}
