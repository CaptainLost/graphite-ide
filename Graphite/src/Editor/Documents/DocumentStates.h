#pragma once

#include "Core/Text/DocumentStore.h"
#include "Editor/Overlays/DiagnosticOverlay.h"
#include "Editor/Overlays/SemanticOverlay.h"
#include "Language/Syntax/SyntaxCache.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <vector>

namespace Graphite {

	class DiagnosticsModel;
	class DocumentServerPort;
	class LspAnswerRouter;

	struct DiagnosticGuard
	{
		std::size_t Revision = 0;
		std::size_t TabWidth = 0;
		std::uint64_t Generation = 0;
	};

	struct DocumentLevelState
	{
		std::optional<SyntaxCache> Syntax;
		SemanticOverlay Semantic;
		DiagnosticOverlay Diagnostics;
		DiagnosticGuard DiagnosticsBuiltFrom;
		bool OpenWithServer = false;
		std::uint64_t SentChange = 0;
	};

	// What every view of a document shares: its colouring, semantic tokens and diagnostics
	// overlay, and its life with the language server. One entry per document in the store,
	// keyed by path; Prune drops the entries whose document has gone.
	class DocumentStates
	{
	public:
		DocumentStates(DocumentServerPort& server, LspAnswerRouter& router, const DiagnosticsModel& diagnostics);

		DocumentLevelState* Find(const std::filesystem::path& path);

		SyntaxCache& SyntaxFor(const std::filesystem::path& path, std::size_t tabWidth);

		SemanticOverlay& SemanticFor(const std::filesystem::path& path);

		DiagnosticOverlay& DiagnosticFor(const std::filesystem::path& path, const Core::TextBuffer& buffer,
										 std::size_t tabWidth);

		bool OpenWithServer(const std::filesystem::path& path, const Core::TextDocument& document);

		bool IsOpenWithServer(const std::filesystem::path& path) const;

		bool SendChangeIfMoved(const std::filesystem::path& path, const Core::TextDocument& document);

		std::vector<std::filesystem::path> SendChanges(const Core::DocumentStore& store);

		void Prune(const Core::DocumentStore& store);

	private:
		DocumentServerPort& m_Server;
		LspAnswerRouter& m_Router;
		const DiagnosticsModel& m_Diagnostics;

		std::map<std::filesystem::path, DocumentLevelState> m_States;
	};
}
