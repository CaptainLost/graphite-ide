#pragma once

#include "Editor/Server/DocumentServerPort.h"

#include "Language/Api/LanguageService.h"
#include "Language/Server/LanguageServices.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>

namespace Graphite {

	struct CaretScreenPosition
	{
		float X = 0.0f;
		float Y = 0.0f;
	};

	struct DefinitionQuestion
	{
		std::filesystem::path Path;
		std::size_t CaretOffset = 0;
		FilePosition From;
		CaretScreenPosition Screen;
	};

	struct HoverQuestion
	{
		std::filesystem::path Path;
		std::size_t DocumentRevision = 0;
		std::size_t Line = 0;
		std::size_t TokenFrom = 0;
		std::size_t TokenTo = 0;
		FilePosition From;

		friend bool operator==(const HoverQuestion&, const HoverQuestion&) = default;
	};

	struct ReferencesQuestion
	{
		std::filesystem::path Path;
		FilePosition Where;
		std::string Symbol;
	};

	enum class QuestionFate : std::uint8_t
	{
		Asked,
		HeldUntilServerIsUp
	};

	class LspAnswerTarget
	{
	public:
		virtual ~LspAnswerTarget() = default;

		LspAnswerTarget() = default;
		LspAnswerTarget(const LspAnswerTarget&) = delete;
		LspAnswerTarget& operator=(const LspAnswerTarget&) = delete;

		virtual void ApplyDefinition(const DefinitionQuestion& question,
									 const LanguageServices::Answer& answer) = 0;
		virtual void ApplyReferences(const ReferencesQuestion& question,
									 const LanguageServices::Answer& answer) = 0;

		virtual void AskedHeldReferences(const ReferencesQuestion& question) = 0;

		virtual void RefreshingReferences(const ReferencesQuestion& question) = 0;

		virtual void ApplySwitchSourceHeader(const std::filesystem::path& from,
											 const LanguageServices::Answer& answer) = 0;

		virtual void ApplyHover(const HoverQuestion& question,
								const LanguageServices::Answer& answer) = 0;
		virtual void ApplyPreviewDefinition(const HoverQuestion& question,
											const LanguageServices::Answer& answer) = 0;

		virtual void ApplySemanticTokens(const std::filesystem::path& path,
										 const LanguageServices::Answer& answer) = 0;

		virtual bool ApplySymbols(const std::filesystem::path& path,
								  const LanguageServices::Answer& answer) = 0;

		virtual void ApplyCount(const std::filesystem::path& path, std::size_t documentRow,
								std::size_t count) = 0;

		virtual void FailCount(const std::filesystem::path& path, std::size_t documentRow) = 0;

		virtual bool Offer(const LanguageServices::Answer& answer) = 0;
	};

	inline constexpr double kFailedRequestRetrySeconds = 2.0;

	inline constexpr double kAskAtOnceSeconds = 0.0;

	class LspAnswerRouter
	{
	public:
		using SecondsSource = std::function<double()>;

		LspAnswerRouter(DocumentServerPort& server, SecondsSource secondsSource);

		void Track(const std::filesystem::path& path);

		void Forget(const std::filesystem::path& path);

		bool NoteServerState(const std::filesystem::path& path);

		void WantSymbolsNow(const std::filesystem::path& path);

		void AskSymbolsIfDue(const std::filesystem::path& path);

		void AskSemanticTokens(const std::filesystem::path& path);

		bool CanCountReferences(const std::filesystem::path& path);

		ServerStatus StatusOf(const std::filesystem::path& path);

		void AskCount(const std::filesystem::path& path, std::size_t documentRow,
					  const FilePosition& where);

		void DropCounts(const std::filesystem::path& path);

		QuestionFate AskDefinition(DefinitionQuestion question);
		QuestionFate AskReferences(ReferencesQuestion question);

		QuestionFate AskSwitchSourceHeader(const std::filesystem::path& path);

		void CancelDefinition();
		void CancelSwitchSourceHeader();

		bool HoldsDefinition() const { return m_HeldDefinition.has_value(); }
		bool HoldsSwitchSourceHeader() const { return m_HeldSwitch.has_value(); }

		const ReferencesQuestion* HeldReferences() const;

		const std::optional<ReferencesQuestion>& LastReferences() const { return m_LastReferences; }

		void AskHover(HoverQuestion question);
		void AskPreviewDefinition(HoverQuestion question);

		void CancelHover(const std::filesystem::path& path);

		bool IsAnalysing(const std::filesystem::path& path) const;

		void Collect(LspAnswerTarget& target);

	private:
		bool IsComingUp(const std::filesystem::path& path);

		void SendDefinition(DefinitionQuestion question);
		void SendReferences(ReferencesQuestion question);
		void SendSwitchSourceHeader(const std::filesystem::path& path);
		void SendHover(HoverQuestion question);
		void SendPreviewDefinition(HoverQuestion question);

		void ReleaseHeldQuestions(LspAnswerTarget& target);

		void AskAgainOnceIndexed(LspAnswerTarget& target);

		struct Schedule
		{
			bool SymbolsWanted = true;
			bool SymbolsOutstanding = false;
			bool WantedWhileOutstanding = false;
			double NextAskAt = kAskAtOnceSeconds;
			ServerState LastServerState = ServerState::NotConfigured;
		};

		struct PendingDefinition
		{
			LanguageServices::Ticket Ticket{};
			DefinitionQuestion Question;
		};

		struct PendingReferences
		{
			LanguageServices::Ticket Ticket{};
			ReferencesQuestion Question;
		};

		struct PendingSwitch
		{
			LanguageServices::Ticket Ticket{};
			std::filesystem::path SourcePath;
		};

		struct PendingHover
		{
			LanguageServices::Ticket Ticket{};
			HoverQuestion Question;
		};

		struct PendingCount
		{
			std::filesystem::path Path;
			std::size_t DocumentRow = 0;
		};

		DocumentServerPort& m_Server;
		SecondsSource m_SecondsSource;

		std::map<std::filesystem::path, Schedule> m_Schedules;

		std::optional<PendingDefinition> m_PendingDefinition;
		std::optional<PendingReferences> m_PendingReferences;
		std::optional<PendingSwitch> m_PendingSwitch;
		std::optional<PendingHover> m_PendingHover;
		std::optional<PendingHover> m_PendingPreview;

		std::optional<DefinitionQuestion> m_HeldDefinition;
		std::optional<ReferencesQuestion> m_HeldReferences;
		std::optional<std::filesystem::path> m_HeldSwitch;
		std::optional<HoverQuestion> m_HeldHover;
		std::optional<HoverQuestion> m_HeldPreview;

		std::optional<ReferencesQuestion> m_LastReferences;

		// WARNING: Set when references were asked of a server still indexing, whose answer may leave out the files it
		// has not reached; they are asked again once the index is complete.
		std::optional<ReferencesQuestion> m_AskAgainOnceIndexed;

		std::map<LanguageServices::Ticket, std::filesystem::path> m_PendingSymbols;
		std::map<LanguageServices::Ticket, std::filesystem::path> m_PendingSemantic;
		std::map<LanguageServices::Ticket, PendingCount> m_PendingCounts;
	};
}
