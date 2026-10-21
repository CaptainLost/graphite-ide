#pragma once

#include "Editor/Server/DocumentServerPort.h"

#include "Language/Api/LanguageService.h"
#include "Language/Server/LanguageServices.h"

#include <cstddef>
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

	class LspAnswerTarget
	{
	public:
		virtual ~LspAnswerTarget() = default;

		LspAnswerTarget() = default;
		LspAnswerTarget(const LspAnswerTarget&) = delete;
		LspAnswerTarget& operator=(const LspAnswerTarget&) = delete;

		virtual void ApplyDefinition(const DefinitionQuestion& question,
									 const LanguageServices::Answer& answer) = 0;
		virtual void ApplyReferences(const std::string& symbol,
									 const LanguageServices::Answer& answer) = 0;

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

		void AskDefinition(DefinitionQuestion question);
		void AskReferences(const std::filesystem::path& path, const FilePosition& where,
						   std::string symbol);

		void AskSwitchSourceHeader(const std::filesystem::path& path);

		void AskHover(HoverQuestion question);
		void AskPreviewDefinition(HoverQuestion question);

		void CancelHover(const std::filesystem::path& path);

		bool IsAnalysing(const std::filesystem::path& path) const;

		void Collect(LspAnswerTarget& target);

	private:
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
			std::string Symbol;
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

		std::map<LanguageServices::Ticket, std::filesystem::path> m_PendingSymbols;
		std::map<LanguageServices::Ticket, std::filesystem::path> m_PendingSemantic;
		std::map<LanguageServices::Ticket, PendingCount> m_PendingCounts;
	};
}
