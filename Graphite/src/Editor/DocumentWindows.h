#pragma once

#include "Editor/ModalGate.h"
#include "Editor/ModalQuestion.h"
#include "Editor/TextEditorWidget.h"

#include "Lang/Syntax/SyntaxCache.h"

#include "Core/Text/Editing.h"
#include "Core/Text/FileSync.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
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
		DocumentWindows(Core::DocumentSet& documents, ModalGate& modals)
			: m_Documents(documents),
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

	private:
		SyntaxCache& SyntaxFor(const Core::OpenItem& item);

		void PruneSyntaxCaches();

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
	};
}