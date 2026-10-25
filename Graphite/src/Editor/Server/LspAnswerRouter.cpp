#include "Editor/Server/LspAnswerRouter.h"

#include "Language/Registry/LanguageRegistry.h"
#include "Language/Server/ServerWaitingText.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace Graphite {

	namespace {

		bool IsReadyToAnswer(ServerState state)
		{
			return state == ServerState::Ready || state == ServerState::Indexing ||
				   state == ServerState::ProjectIncomplete;
		}

		bool AskingAgainCannotHelp(const LanguageServices::Answer& answer)
		{
			return answer.Failed && answer.FailedBecause != ReplyFailure::ServerUnavailable;
		}

		bool AnyTicketIsFor(const std::map<LanguageServices::Ticket, std::filesystem::path>& tickets,
							const std::filesystem::path& path)
		{
			for (const auto& [ticket, ticketPath] : tickets)
			{
				if (ticketPath == path)
				{
					return true;
				}
			}

			return false;
		}

		void EraseTicketsFor(std::map<LanguageServices::Ticket, std::filesystem::path>& tickets,
							 const std::filesystem::path& path)
		{
			for (std::map<LanguageServices::Ticket, std::filesystem::path>::iterator ticketEntry =
					 tickets.begin();
				 ticketEntry != tickets.end();)
			{
				const auto& [ticket, ticketPath] = *ticketEntry;

				if (ticketPath == path)
				{
					ticketEntry = tickets.erase(ticketEntry);
				}
				else
				{
					++ticketEntry;
				}
			}
		}
	}

	LspAnswerRouter::LspAnswerRouter(DocumentServerPort& server, SecondsSource secondsSource)
		: m_Server(server), m_SecondsSource(std::move(secondsSource))
	{
	}

	void LspAnswerRouter::Track(const std::filesystem::path& path)
	{
		m_Schedules.try_emplace(path);
	}

	void LspAnswerRouter::Forget(const std::filesystem::path& path)
	{
		m_Schedules.erase(path);

		EraseTicketsFor(m_PendingSemantic, path);
		EraseTicketsFor(m_PendingSymbols, path);

		for (std::map<LanguageServices::Ticket, PendingCount>::iterator countEntry =
				 m_PendingCounts.begin();
			 countEntry != m_PendingCounts.end();)
		{
			const auto& [ticket, pendingCount] = *countEntry;

			if (pendingCount.Path == path)
			{
				countEntry = m_PendingCounts.erase(countEntry);
			}
			else
			{
				++countEntry;
			}
		}
	}

	bool LspAnswerRouter::NoteServerState(const std::filesystem::path& path)
	{
		if (!m_Schedules.contains(path))
		{
			return false;
		}

		Schedule& schedule = m_Schedules.at(path);

		const ServerState currentState = m_Server.StatusFor(path, LanguageOfPath(path)).State;

		if (currentState == schedule.LastServerState)
		{
			return false;
		}

		const ServerState previousState = schedule.LastServerState;

		schedule.LastServerState = currentState;

		const bool handshakeJustFinished =
			previousState == ServerState::Starting && IsReadyToAnswer(currentState);

		if (handshakeJustFinished)
		{
			AskSemanticTokens(path);
		}

		const bool serverWasReplaced = currentState == ServerState::Starting;

		if (!serverWasReplaced)
		{
			return false;
		}

		schedule.SymbolsWanted = true;
		schedule.NextAskAt = kAskAtOnceSeconds;
		schedule.SymbolsOutstanding = false;

		DropCounts(path);

		return true;
	}

	void LspAnswerRouter::WantSymbolsNow(const std::filesystem::path& path)
	{
		if (m_Schedules.contains(path))
		{
			Schedule& schedule = m_Schedules.at(path);

			schedule.SymbolsWanted = true;
			schedule.NextAskAt = kAskAtOnceSeconds;

			if (schedule.SymbolsOutstanding)
			{
				schedule.WantedWhileOutstanding = true;
			}
		}
	}

	void LspAnswerRouter::AskSymbolsIfDue(const std::filesystem::path& path)
	{
		if (!m_Schedules.contains(path))
		{
			return;
		}

		Schedule& schedule = m_Schedules.at(path);
		const double nowSeconds = m_SecondsSource();

		const bool notDue = !schedule.SymbolsWanted || schedule.SymbolsOutstanding ||
							nowSeconds < schedule.NextAskAt;

		if (notDue)
		{
			return;
		}

		const LanguageServices::Ticket ticket = m_Server.AskSymbols(path, LanguageOfPath(path));

		m_PendingSymbols.emplace(ticket, path);

		AskSemanticTokens(path);

		schedule.SymbolsOutstanding = true;
		schedule.WantedWhileOutstanding = false;
		schedule.NextAskAt = nowSeconds + kFailedRequestRetrySeconds;
	}

	void LspAnswerRouter::AskSemanticTokens(const std::filesystem::path& path)
	{
		const LanguageServices::Ticket ticket =
			m_Server.AskSemanticTokens(path, LanguageOfPath(path));

		m_PendingSemantic.emplace(ticket, path);
	}

	bool LspAnswerRouter::CanCountReferences(const std::filesystem::path& path)
	{
		const ServerState currentState = m_Server.StatusFor(path, LanguageOfPath(path)).State;

		return currentState == ServerState::Ready || currentState == ServerState::ProjectIncomplete;
	}

	ServerStatus LspAnswerRouter::StatusOf(const std::filesystem::path& path)
	{
		return m_Server.StatusFor(path, LanguageOfPath(path));
	}

	void LspAnswerRouter::AskCount(const std::filesystem::path& path, std::size_t documentRow,
								   const FilePosition& where)
	{
		const LanguageServices::Ticket ticket =
			m_Server.AskReferences(path, LanguageOfPath(path), where);

		m_PendingCounts.emplace(ticket, PendingCount{.Path = path, .DocumentRow = documentRow});
	}

	void LspAnswerRouter::DropCounts(const std::filesystem::path& path)
	{
		for (std::map<LanguageServices::Ticket, PendingCount>::iterator countEntry =
				 m_PendingCounts.begin();
			 countEntry != m_PendingCounts.end();)
		{
			const auto& [ticket, pendingCount] = *countEntry;

			if (pendingCount.Path == path)
			{
				m_Server.Cancel(ticket);
				countEntry = m_PendingCounts.erase(countEntry);
			}
			else
			{
				++countEntry;
			}
		}
	}

	bool LspAnswerRouter::IsComingUp(const std::filesystem::path& path)
	{
		return AvailabilityOf(StatusOf(path)) == ServerAvailability::ComingUp;
	}

	void LspAnswerRouter::CancelDefinition()
	{
		if (m_PendingDefinition)
		{
			m_Server.Cancel(m_PendingDefinition->Ticket);
			m_PendingDefinition.reset();
		}

		m_HeldDefinition.reset();
	}

	void LspAnswerRouter::CancelSwitchSourceHeader()
	{
		if (m_PendingSwitch)
		{
			m_Server.Cancel(m_PendingSwitch->Ticket);
			m_PendingSwitch.reset();
		}

		m_HeldSwitch.reset();
	}

	const ReferencesQuestion* LspAnswerRouter::HeldReferences() const
	{
		return m_HeldReferences ? &*m_HeldReferences : nullptr;
	}

	// WARNING: A question is held, not sent, while the server starts or loads its projects: Roslyn answers an empty
	// list then, which would read as there being nothing to find.
	QuestionFate LspAnswerRouter::AskDefinition(DefinitionQuestion question)
	{
		CancelDefinition();

		m_Server.EnsureStarted(question.Path, LanguageOfPath(question.Path));

		if (IsComingUp(question.Path))
		{
			m_HeldDefinition = std::move(question);
			return QuestionFate::HeldUntilServerIsUp;
		}

		SendDefinition(std::move(question));
		return QuestionFate::Asked;
	}

	void LspAnswerRouter::SendDefinition(DefinitionQuestion question)
	{
		const LanguageServices::Ticket ticket =
			m_Server.AskDefinition(question.Path, LanguageOfPath(question.Path), question.From);

		m_PendingDefinition = PendingDefinition{.Ticket = ticket, .Question = std::move(question)};
	}

	QuestionFate LspAnswerRouter::AskReferences(ReferencesQuestion question)
	{
		if (m_PendingReferences)
		{
			m_Server.Cancel(m_PendingReferences->Ticket);
			m_PendingReferences.reset();
		}

		m_HeldReferences.reset();
		m_AskAgainOnceIndexed.reset();
		m_LastReferences = question;

		m_Server.EnsureStarted(question.Path, LanguageOfPath(question.Path));

		if (IsComingUp(question.Path))
		{
			m_HeldReferences = std::move(question);
			return QuestionFate::HeldUntilServerIsUp;
		}

		SendReferences(std::move(question));
		return QuestionFate::Asked;
	}

	void LspAnswerRouter::SendReferences(ReferencesQuestion question)
	{
		if (IsStillIndexing(StatusOf(question.Path)))
		{
			m_AskAgainOnceIndexed = question;
		}

		const LanguageServices::Ticket ticket =
			m_Server.AskReferences(question.Path, LanguageOfPath(question.Path), question.Where);

		m_PendingReferences = PendingReferences{.Ticket = ticket, .Question = std::move(question)};
	}

	QuestionFate LspAnswerRouter::AskSwitchSourceHeader(const std::filesystem::path& path)
	{
		CancelSwitchSourceHeader();

		m_Server.EnsureStarted(path, LanguageOfPath(path));

		if (IsComingUp(path))
		{
			m_HeldSwitch = path;
			return QuestionFate::HeldUntilServerIsUp;
		}

		SendSwitchSourceHeader(path);
		return QuestionFate::Asked;
	}

	void LspAnswerRouter::SendSwitchSourceHeader(const std::filesystem::path& path)
	{
		const LanguageServices::Ticket ticket = m_Server.AskSwitchSourceHeader(path, LanguageOfPath(path));

		m_PendingSwitch = PendingSwitch{.Ticket = ticket, .SourcePath = path};
	}

	void LspAnswerRouter::AskHover(HoverQuestion question)
	{
		if (m_PendingHover)
		{
			m_Server.Cancel(m_PendingHover->Ticket);
			m_PendingHover.reset();
		}

		m_HeldHover.reset();

		m_Server.EnsureStarted(question.Path, LanguageOfPath(question.Path));

		if (IsComingUp(question.Path))
		{
			m_HeldHover = std::move(question);
			return;
		}

		SendHover(std::move(question));
	}

	void LspAnswerRouter::SendHover(HoverQuestion question)
	{
		const LanguageServices::Ticket ticket =
			m_Server.AskHover(question.Path, LanguageOfPath(question.Path), question.From);

		m_PendingHover = PendingHover{.Ticket = ticket, .Question = std::move(question)};
	}

	void LspAnswerRouter::AskPreviewDefinition(HoverQuestion question)
	{
		if (m_PendingPreview)
		{
			m_Server.Cancel(m_PendingPreview->Ticket);
			m_PendingPreview.reset();
		}

		m_HeldPreview.reset();

		m_Server.EnsureStarted(question.Path, LanguageOfPath(question.Path));

		if (IsComingUp(question.Path))
		{
			m_HeldPreview = std::move(question);
			return;
		}

		SendPreviewDefinition(std::move(question));
	}

	void LspAnswerRouter::SendPreviewDefinition(HoverQuestion question)
	{
		const LanguageServices::Ticket ticket =
			m_Server.AskDefinition(question.Path, LanguageOfPath(question.Path), question.From);

		m_PendingPreview = PendingHover{.Ticket = ticket, .Question = std::move(question)};
	}

	void LspAnswerRouter::CancelHover(const std::filesystem::path& path)
	{
		if (m_PendingHover && m_PendingHover->Question.Path == path)
		{
			m_Server.Cancel(m_PendingHover->Ticket);
			m_PendingHover.reset();
		}

		if (m_PendingPreview && m_PendingPreview->Question.Path == path)
		{
			m_Server.Cancel(m_PendingPreview->Ticket);
			m_PendingPreview.reset();
		}

		if (m_HeldHover && m_HeldHover->Path == path)
		{
			m_HeldHover.reset();
		}

		if (m_HeldPreview && m_HeldPreview->Path == path)
		{
			m_HeldPreview.reset();
		}
	}

	void LspAnswerRouter::ReleaseHeldQuestions(LspAnswerTarget& target)
	{
		if (m_HeldDefinition && !IsComingUp(m_HeldDefinition->Path))
		{
			DefinitionQuestion released = std::move(*m_HeldDefinition);
			m_HeldDefinition.reset();
			SendDefinition(std::move(released));
		}

		if (m_HeldReferences && !IsComingUp(m_HeldReferences->Path))
		{
			ReferencesQuestion released = std::move(*m_HeldReferences);
			m_HeldReferences.reset();
			target.AskedHeldReferences(released);
			SendReferences(std::move(released));
		}

		if (m_HeldSwitch && !IsComingUp(*m_HeldSwitch))
		{
			const std::filesystem::path released = std::move(*m_HeldSwitch);
			m_HeldSwitch.reset();
			SendSwitchSourceHeader(released);
		}

		if (m_HeldHover && !IsComingUp(m_HeldHover->Path))
		{
			HoverQuestion released = std::move(*m_HeldHover);
			m_HeldHover.reset();
			SendHover(std::move(released));
		}

		if (m_HeldPreview && !IsComingUp(m_HeldPreview->Path))
		{
			HoverQuestion released = std::move(*m_HeldPreview);
			m_HeldPreview.reset();
			SendPreviewDefinition(std::move(released));
		}
	}

	void LspAnswerRouter::AskAgainOnceIndexed(LspAnswerTarget& target)
	{
		if (!m_AskAgainOnceIndexed || m_PendingReferences || m_HeldReferences)
		{
			return;
		}

		const ServerStatus status = StatusOf(m_AskAgainOnceIndexed->Path);

		if (IsStillIndexing(status))
		{
			return;
		}

		ReferencesQuestion again = std::move(*m_AskAgainOnceIndexed);
		m_AskAgainOnceIndexed.reset();

		if (AvailabilityOf(status) != ServerAvailability::Answers)
		{
			return;
		}

		target.RefreshingReferences(again);
		SendReferences(std::move(again));
	}

	bool LspAnswerRouter::IsAnalysing(const std::filesystem::path& path) const
	{
		if (m_Schedules.contains(path))
		{
			const Schedule& schedule = m_Schedules.at(path);
			const bool symbolsWantedOrAsked = schedule.SymbolsOutstanding || schedule.SymbolsWanted;

			if (symbolsWantedOrAsked)
			{
				return true;
			}
		}

		return AnyTicketIsFor(m_PendingSemantic, path) || AnyTicketIsFor(m_PendingSymbols, path);
	}

	void LspAnswerRouter::Collect(LspAnswerTarget& target)
	{
		std::vector<std::filesystem::path> recolouredPaths;
		m_Server.TakeRecolour(recolouredPaths);

		std::sort(recolouredPaths.begin(), recolouredPaths.end());
		recolouredPaths.erase(std::unique(recolouredPaths.begin(), recolouredPaths.end()),
							  recolouredPaths.end());

		for (const std::filesystem::path& recolouredPath : recolouredPaths)
		{
			if (m_Schedules.contains(recolouredPath))
			{
				AskSemanticTokens(recolouredPath);
			}
		}

		ReleaseHeldQuestions(target);

		std::vector<LanguageServices::Answer> answers;

		m_Server.TakeAnswers(answers);

		for (const LanguageServices::Answer& answer : answers)
		{
			if (m_PendingDefinition && m_PendingDefinition->Ticket == answer.For)
			{
				const PendingDefinition pending = std::move(*m_PendingDefinition);

				m_PendingDefinition.reset();
				target.ApplyDefinition(pending.Question, answer);
				continue;
			}

			if (m_PendingReferences && m_PendingReferences->Ticket == answer.For)
			{
				const PendingReferences pending = std::move(*m_PendingReferences);

				m_PendingReferences.reset();

				if (answer.Failed)
				{
					m_AskAgainOnceIndexed.reset();
				}

				target.ApplyReferences(pending.Question, answer);
				continue;
			}

			if (m_PendingSwitch && m_PendingSwitch->Ticket == answer.For)
			{
				const PendingSwitch pending = std::move(*m_PendingSwitch);

				m_PendingSwitch.reset();
				target.ApplySwitchSourceHeader(pending.SourcePath, answer);
				continue;
			}

			if (m_PendingHover && m_PendingHover->Ticket == answer.For)
			{
				const PendingHover pending = std::move(*m_PendingHover);

				m_PendingHover.reset();
				target.ApplyHover(pending.Question, answer);
				continue;
			}

			if (m_PendingPreview && m_PendingPreview->Ticket == answer.For)
			{
				const PendingHover pending = std::move(*m_PendingPreview);

				m_PendingPreview.reset();
				target.ApplyPreviewDefinition(pending.Question, answer);
				continue;
			}

			std::map<LanguageServices::Ticket, std::filesystem::path>::node_type semanticNode =
				m_PendingSemantic.extract(answer.For);

			if (!semanticNode.empty())
			{
				if (!answer.Failed)
				{
					target.ApplySemanticTokens(semanticNode.mapped(), answer);
				}

				continue;
			}

			std::map<LanguageServices::Ticket, std::filesystem::path>::node_type symbolsNode =
				m_PendingSymbols.extract(answer.For);

			if (!symbolsNode.empty())
			{
				const std::filesystem::path& path = symbolsNode.mapped();

				if (m_Schedules.contains(path))
				{
					Schedule& schedule = m_Schedules.at(path);

					schedule.SymbolsOutstanding = AnyTicketIsFor(m_PendingSymbols, path);

					const bool applied = !answer.Failed && target.ApplySymbols(path, answer);
					const bool lastQuestionFailedForGood = AskingAgainCannotHelp(answer) && !schedule.SymbolsOutstanding;

					// WARNING: The answer describes the text as it was when it was asked, so a want
					// that arrived since, from an edit or a save, keeps symbols wanted.
					if (applied || lastQuestionFailedForGood)
					{
						schedule.SymbolsWanted = schedule.WantedWhileOutstanding;
						schedule.WantedWhileOutstanding = false;
					}
				}

				continue;
			}

			if (target.Offer(answer))
			{
				continue;
			}

			std::map<LanguageServices::Ticket, PendingCount>::node_type countNode =
				m_PendingCounts.extract(answer.For);

			if (countNode.empty())
			{
				continue;
			}

			const PendingCount pending = countNode.mapped();

			if (answer.Failed)
			{
				target.FailCount(pending.Path, pending.DocumentRow);
				continue;
			}

			target.ApplyCount(pending.Path, pending.DocumentRow, answer.Locations.size());
		}

		AskAgainOnceIndexed(target);
	}
}
