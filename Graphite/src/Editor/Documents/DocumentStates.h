#pragma once

#include "Core/Text/DocumentStore.h"
#include "Editor/Overlays/DiagnosticOverlay.h"
#include "Editor/Overlays/SemanticOverlay.h"
#include "Language/Syntax/SyntaxCache.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
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

		// The newest change this state has noticed and when it noticed it, which is what a large
		// document waits on before its text is sent.
		std::uint64_t NoticedChange = 0;
		std::chrono::steady_clock::time_point NoticedAt;
	};

	enum class SendTiming
	{
		// A large document is sent once it has stopped changing; a small one at once.
		WhenQuiet,
		Now
	};

	// What every view of a document shares: its colouring, semantic tokens and diagnostics
	// overlay, and its life with the language server. Entries are made on demand,
	// keyed by path, for documents in the store; Prune drops the ones whose document has gone.
	class DocumentStates
	{
	public:
		using Clock = std::chrono::steady_clock;
		using ClockReader = std::function<Clock::time_point()>;

		// Text past this many bytes is copied whole for the server on every change, so it is sent
		// only after the document has been quiet for kLargeDocumentQuiet.
		static constexpr std::size_t kLargeDocumentBytes = 1024 * 1024;
		static constexpr std::chrono::milliseconds kLargeDocumentQuiet{150};

		DocumentStates(DocumentServerPort& server, LspAnswerRouter& router, const DiagnosticsModel& diagnostics,
					   ClockReader now = [] { return Clock::now(); });

		DocumentLevelState* Find(const std::filesystem::path& path);

		SyntaxCache& SyntaxFor(const std::filesystem::path& path, std::size_t tabWidth);

		SemanticOverlay& SemanticFor(const std::filesystem::path& path);

		DiagnosticOverlay& DiagnosticFor(const std::filesystem::path& path, const Core::TextBuffer& buffer,
										 std::size_t tabWidth);

		bool OpenWithServer(const std::filesystem::path& path, const Core::TextDocument& document);

		bool IsOpenWithServer(const std::filesystem::path& path) const;

		bool SendChangeIfMoved(const std::filesystem::path& path, const Core::TextDocument& document,
							   SendTiming timing = SendTiming::WhenQuiet);

		std::vector<std::filesystem::path> SendChanges(const Core::DocumentStore& store);

		void NoteSaved(const std::filesystem::path& path);

		void Prune(const Core::DocumentStore& store);

	private:
		DocumentServerPort& m_Server;
		LspAnswerRouter& m_Router;
		const DiagnosticsModel& m_Diagnostics;
		ClockReader m_Now;

		std::map<std::filesystem::path, DocumentLevelState> m_States;
	};
}
