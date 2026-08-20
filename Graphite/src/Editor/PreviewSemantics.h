#pragma once

#include "Editor/SemanticOverlay.h"

#include "Lang/Api/LanguageId.h"
#include "Lang/Server/LanguageServices.h"

#include "Core/Text/Columns.h"
#include "Core/Text/TextBuffer.h"

#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	// What PreviewSemantics needs from a language server and nothing more.
	//
	// Separate from LanguageServices so the bookkeeping below — which is all this feature is —
	// can be tested without a clangd process: the tests hand it a port that records what it was
	// told.
	class PreviewServerPort
	{
	public:
		virtual ~PreviewServerPort() = default;

		PreviewServerPort() = default;
		PreviewServerPort(const PreviewServerPort&) = delete;
		PreviewServerPort& operator=(const PreviewServerPort&) = delete;

		virtual void Open(const std::filesystem::path& path, LanguageId language,
						  std::string text) = 0;
		virtual LanguageServices::Ticket Ask(const std::filesystem::path& path,
											 LanguageId language) = 0;
		virtual void Close(const std::filesystem::path& path, LanguageId language) = 0;
	};

	// The semantic colours of the files preview panes are showing right now.
	//
	// Until this existed, only a tab opened a document with a server. A preview pane shows a file
	// without opening one, so it asks here instead, and this tells the server the file is open for
	// exactly as long as somebody is looking at it.
	//
	// Keyed by path and counted by declaration: two panes showing one file share a single didOpen,
	// a single request and a single overlay. Keying by pane would parse the same translation unit
	// twice, and a single shared slot would thrash didOpen/didClose every frame whenever two
	// visible panes showed different files.
	class PreviewSemantics
	{
	public:
		explicit PreviewSemantics(PreviewServerPort& port) : m_Port(port) {}

		// Closes whatever it opened. A preview pane outliving the server bookkeeping would leave
		// documents open for the rest of the session.
		~PreviewSemantics();

		PreviewSemantics(const PreviewSemantics&) = delete;
		PreviewSemantics& operator=(const PreviewSemantics&) = delete;

		// Once at the top of a frame, before any pane declares anything: closes entries nothing
		// declared through a whole frame, and re-asks a request that failed once its retry clock
		// has elapsed.
		void Tick(std::size_t frame, double now);

		// Every frame, from the pane showing this file. The first declaration of a path opens it
		// with the server — unless `editorHolds`, because then a tab already did — and asks for
		// its tokens. Later declarations of the same path cost a map lookup and a stamp.
		//
		// `editorHolds` also decides whether the buffer's revision is worth watching: a file this
		// class opened cannot change under it, while a file with a tab is being edited.
		void Declare(const std::filesystem::path& path, const Core::TextBuffer& buffer,
					 std::size_t tabWidth, bool editorHolds);

		// Empty for a file nothing is known about yet, which includes every file between the
		// frame it is first declared in and the frame its answer arrives.
		const std::vector<SemanticSpan>& SpansOn(const std::filesystem::path& path,
												 std::size_t line) const;

		// True when this answer was one of ours, whether or not it was any use. The caller routes
		// by return value: an answer nothing here asked for belongs to somebody else.
		bool Apply(const LanguageServices::Answer& answer);

	private:
		struct Entry
		{
			SemanticOverlay Overlay;

			// The exact text the outstanding request is about. Kept because the answer's columns
			// are only meaningful against it: converting against whatever the buffer holds when
			// the reply lands would put spans on words that have since moved.
			Core::TextBuffer Snapshot;

			std::size_t TabWidth = Core::DefaultTabWidth;
			std::size_t Revision = 0;

			bool EditorHolds = false;

			// Whether *this* class sent the didOpen. A file with a tab must never be closed from
			// here — the editor is still drawing it.
			bool Opened = false;

			std::optional<LanguageServices::Ticket> Ticket;

			// When a failed request may be tried again. Zero means there is nothing to retry.
			double NextAskAt = 0.0;

			std::size_t LastDeclared = 0;
		};

		void Ask(const std::filesystem::path& path, Entry& entry);

		PreviewServerPort& m_Port;

		std::map<std::filesystem::path, Entry> m_Entries;

		std::size_t m_Frame = 0;
		double m_Now = 0.0;
	};
}
