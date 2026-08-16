#pragma once

#include "Editor/ModalGate.h"
#include "Editor/FindBar.h"
#include "Editor/LensModel.h"
#include "Editor/ModalQuestion.h"
#include "Editor/TextEditorWidget.h"

#include "Lang/Server/LanguageServices.h"
#include "Lang/Syntax/SyntaxCache.h"

#include "Core/Text/Editing.h"
#include "Core/Text/FileSync.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace Core {

	class CommandRegistry;
	class Keymap;
}
namespace Core {

	class DocumentSet;

}

namespace Graphite {

	class DocumentWindows
	{
	public:
		DocumentWindows(Core::DocumentSet& documents, ModalGate& modals,
						LanguageServices& languageServices)
			: m_Documents(documents),
			  m_LanguageServices(languageServices),
			  m_SizeQuestion(modals, kSizeQuestionTitle),
			  m_ReloadConflict(modals, kReloadConflictTitle),
			  m_OverwriteConfirm(modals, kOverwriteConfirmTitle),
			  m_CloseConfirm(modals, kCloseConfirmTitle)
		{
		}

		void Render(const CommandContext& context, float fontSize,
					Core::IndentStyle indentStyle, unsigned int fallbackDockNode,
					bool wordWrap, const ResolvedScheme& scheme);

		void RequestOpen(const std::filesystem::path& path, bool permanent);

		void HandleRootMoved(const std::filesystem::path& from, const std::filesystem::path& to);

		void HandleFilesChanged(const std::vector<std::filesystem::path>& directories,
								bool everything);

		void NotifyWindowFocus(bool focusedNow);

		bool SaveAllModified();

		bool HasModifiedDocuments() const;

		void RegisterCommands(Core::CommandRegistry& commands, Core::Keymap& keys);

		std::optional<std::string> GetStatusMessage() const;

		// Opens a document's find bar. Routed through here rather than called on the bar directly,
		// because the bar belongs to whatever draws the documents and the command that binds
		// Ctrl+F to it has no business knowing where that is.
		void OpenFind(Core::OpenItem& item) { m_FindBar.Open(item); }

	private:
		// Everything the reference-count rows need for one document. LensModel alone is not
		// enough: the declarations behind it have to be kept so the anchors can be rebuilt when
		// the wrap width changes — a lens anchors to a ROW, and a rewrap moves every row without
		// any declaration having moved.
		struct LensState
		{
			LensModel Model;

			std::vector<Declaration> Declarations;

			// The wrap width the anchors were last built at, so a rewrap is noticed. Only
			// meaningful once a reply has arrived.
			std::size_t BuiltAt = 0;

			bool HaveDeclarations = false;

			// Document rows an AskReferences is already outstanding for, so a row visible for a
			// second is asked about once rather than sixty times. Cleared whenever the anchors
			// move, because the rows it holds no longer mean what they did.
			std::set<std::size_t> Asked;

			// Symbols are wanted from the moment the document opens and again after every edit.
			// One request may be outstanding at a time, and a failed one — a server still
			// starting, or still indexing — is retried on a clock rather than on the next frame,
			// which would be one request per frame for as long as the server took.
			bool WantSymbols = true;
			bool SymbolsOutstanding = false;
			double NextSymbolsAt = 0.0;
		};

		SyntaxCache& SyntaxFor(const Core::OpenItem& item);

		void PruneSyntaxCaches();

		// Null when no language server is configured for this document's language, which is what
		// puts a null LensModel on EditorView and spares the widget a branch on whether a server
		// exists. Opens the document with the server the first time it is asked for.
		LensState* LensFor(const Core::OpenItem& item);

		void PruneLensStates();

		// Sends what the frame's edits and geometry mean for this document's lenses, and asks for
		// the counts the rows on screen still need.
		void UpdateLenses(const Core::OpenItem& item, LensState& state,
						  const EditorFrameResult& frame);

		void AskForSymbols(const Core::OpenItem& item, LensState& state);

		void AskForVisibleCounts(const Core::OpenItem& item, LensState& state,
								 const EditorFrameResult& frame);

		// Routes everything the servers have answered since the last frame to the document that
		// asked. LanguageServices::TakeAnswers is the single drain point, so this is the only
		// place answers are collected.
		void CollectServerAnswers();

		void ApplySymbolAnswer(const std::filesystem::path& path,
							   const LanguageServices::Answer& answer);

		void SyncEveryDocument();

		void RespondToSync(std::size_t index, Core::FileSyncOutcome outcome);

		void ReloadOne(std::size_t index);

		void AskAboutSize(const std::filesystem::path& path, bool permanent);

		void LoadConfirmed(const std::filesystem::path& path, bool permanent);

		bool SaveOne(std::size_t index);

		void RequestSave(std::size_t index);

		void SetTransientMessage(std::string text);

		unsigned int DockNodeForIdentity(const std::string& identity) const;

		void MovePendingDockTarget(const std::string& oldIdentity,
								   const std::string& newIdentity);

		void RenderSizeQuestion();

		void RenderReloadConflict();

		void RenderOverwriteConfirm();

		void RenderCloseConfirm();

		bool StillNeedsCloseConfirmation(const std::filesystem::path& path);

		void RenderDocumentWindows(const CommandContext& context, float fontSize,
								   Core::IndentStyle indentStyle, unsigned int fallbackDockNode,
								   bool wordWrap, const ResolvedScheme& scheme,
								   const std::optional<std::filesystem::path>& focusRequest);

		bool IsNewSinceLastFrame(const std::string& identity) const;

		unsigned int ChooseDockTarget(const std::string& identity,
									  unsigned int fallbackDockNode);

		void ActivateAndRememberNode(std::size_t index);

		void CloseAndForgetLayout(std::size_t index);

		void CloseIfOpen(const std::filesystem::path& path);

		void ForgetLayoutOfWindowsClosedElsewhere(
			const std::vector<std::string>& startingIdentities);

		static constexpr const char* kSizeQuestionTitle = "Large file##EditorSizeQuestion";
		static constexpr const char* kReloadConflictTitle =
			"File changed on disk##EditorReloadConflict";
		static constexpr const char* kOverwriteConfirmTitle =
			"Overwrite unsaved changes on disk?##EditorOverwriteConfirm";
		static constexpr const char* kCloseConfirmTitle = "Unsaved changes##EditorCloseConfirm";

		struct LargeFileQuestion
		{
			std::filesystem::path path;
			bool permanent = false;
			std::uint64_t size = 0;

			bool operator==(const LargeFileQuestion&) const = default;
		};

		Core::DocumentSet& m_Documents;

		LanguageServices& m_LanguageServices;

		unsigned int m_LastFocusedNode = 0;

		ModalQuestion<LargeFileQuestion> m_SizeQuestion;

		std::vector<std::string> m_PreviousIdentities;

		std::unordered_map<std::string, unsigned int> m_PendingDockTargets;

		bool m_WasFocused = true;

		ModalQuestion<std::filesystem::path> m_ReloadConflict;

		ModalQuestion<std::filesystem::path> m_OverwriteConfirm;

		std::optional<std::filesystem::path> m_FocusRequest;

		ModalQuestion<std::filesystem::path> m_CloseConfirm;

		struct TransientMessage
		{
			std::string text;
			double expiresAt = 0.0;
		};

		std::optional<TransientMessage> m_TransientMessage;

		// Keyed by path rather than by index: the document list is reordered by dragging tabs
		// and compacted on close, and either would hand a document another one's colours.
		//
		// A side-map on a container that owns the identity: Core::DocumentSet::RewritePaths can
		// change item.Path (a rename on disk, or of a directory an open document lives under)
		// without telling this map, so the entry under the old path would otherwise sit here
		// for the rest of the process. PruneSyntaxCaches is what recovers that rather than a
		// rewrite at the rename site — the rename happens in Core, this map is Graphite's own,
		// and a prune is the one fix that does not need the rename to know this map exists.
		std::map<std::filesystem::path, SyntaxCache> m_Syntax;

		// Keyed and pruned exactly as m_Syntax is, and for the same reason: DocumentSet owns the
		// path this is keyed on and can rewrite it without telling this map, so an entry orphaned
		// by a rename would otherwise hold its declarations for the rest of the process.
		std::map<std::filesystem::path, LensState> m_Lenses;

		// What an outstanding ticket was asked for. Tickets are unique across every server, which
		// is what lets one map per question kind route an answer back to the document — and to the
		// row — that wanted it, without either server's numbering colliding with the other's.
		std::map<LanguageServices::Ticket, std::filesystem::path> m_PendingSymbols;

		struct PendingCount
		{
			std::filesystem::path Path;
			std::size_t DocumentRow = 0;
		};

		std::map<LanguageServices::Ticket, PendingCount> m_PendingCounts;

		// One for every document rather than one each: what it holds is which document wants the
		// keyboard next, and only one can.
		FindBar m_FindBar;
	};
}