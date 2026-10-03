#include "Editor/Server/LspAnswerRouter.h"

#include "Language/Registry/LanguageRegistry.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace Graphite {

	LspAnswerRouter::LspAnswerRouter(DocumentServerPort& server, Clock clock)
		: m_Server(server), m_Clock(std::move(clock))
	{
	}

	void LspAnswerRouter::Track(const std::filesystem::path& path)
	{
		m_Schedules.try_emplace(path);
	}

	void LspAnswerRouter::Forget(const std::filesystem::path& path)
	{
		m_Schedules.erase(path);

		// A ticket is only ever taken out of these when its answer arrives, so a document closed
		// with requests outstanding leaves entries behind that nothing collects if the server
		// never answers them. IsAnalysing reads two of them, and would then report that a file
		// nobody has open is still being looked at - for the rest of the session.
		std::erase_if(m_PendingSemantic, [&path](const auto& entry) { return entry.second == path; });
		std::erase_if(m_PendingSymbols, [&path](const auto& entry) { return entry.second == path; });
		std::erase_if(m_PendingCounts,
					  [&path](const auto& entry) { return entry.second.Path == path; });
	}

	bool LspAnswerRouter::NoteServerState(const std::filesystem::path& path)
	{
		const auto found = m_Schedules.find(path);

		if (found == m_Schedules.end())
		{
			return false;
		}

		Schedule& schedule = found->second;

		const ServerState now = m_Server.StatusFor(path, LanguageOfPath(path)).State;

		if (now == schedule.LastServerState)
		{
			return false;
		}

		const ServerState before = schedule.LastServerState;

		schedule.LastServerState = now;

		// The handshake is what gives the client its semantic legend, and a question asked before
		// it is refused. The retry clock only re-asks while declarations are still wanted, so a
		// server that answered symbols and then finished its handshake would otherwise leave the
		// document uncoloured until the next edit.
		if (before == ServerState::Starting &&
			(now == ServerState::Ready || now == ServerState::Indexing ||
			 now == ServerState::NoCompileDatabase))
		{
			AskSemanticTokens(path);
		}

		// A server that has just entered Starting is a server that has just been replaced -
		// LanguageServices::RestartServer sets exactly that state, and the client it replaced
		// failed every request it was holding.
		if (now != ServerState::Starting)
		{
			return false;
		}

		schedule.Want = true;
		schedule.NextAt = 0.0;
		schedule.Outstanding = false;

		DropCounts(path);

		return true;
	}

	void LspAnswerRouter::WantSymbolsNow(const std::filesystem::path& path)
	{
		if (const auto found = m_Schedules.find(path); found != m_Schedules.end())
		{
			found->second.Want = true;
			found->second.NextAt = 0.0;
		}
	}

	void LspAnswerRouter::AskSymbolsIfDue(const std::filesystem::path& path)
	{
		const auto found = m_Schedules.find(path);

		if (found == m_Schedules.end())
		{
			return;
		}

		Schedule& schedule = found->second;
		const double now = m_Clock();

		if (!schedule.Want || schedule.Outstanding || now < schedule.NextAt)
		{
			return;
		}

		const LanguageServices::Ticket ticket = m_Server.AskSymbols(path, LanguageOfPath(path));

		m_PendingSymbols.emplace(ticket, path);

		// On the same beat, because they answer the same question at different grain - what this
		// document contains - and go stale together on the same edit. Asking on one schedule is
		// one thing to reason about rather than two that drift.
		AskSemanticTokens(path);

		schedule.Outstanding = true;
		schedule.NextAt = now + kSymbolsRetrySeconds;
	}

	void LspAnswerRouter::AskSemanticTokens(const std::filesystem::path& path)
	{
		const LanguageServices::Ticket colours =
			m_Server.AskSemanticTokens(path, LanguageOfPath(path));

		m_PendingSemantic.emplace(colours, path);
	}

	void LspAnswerRouter::AskCount(const std::filesystem::path& path, std::size_t documentRow,
								   const FilePosition& where)
	{
		const LanguageServices::Ticket ticket =
			m_Server.AskReferences(path, LanguageOfPath(path), where);

		m_PendingCounts.emplace(ticket, PendingCount{path, documentRow});
	}

	void LspAnswerRouter::DropCounts(const std::filesystem::path& path)
	{
		// The rows in flight were asked about before the text moved, so their answers are about
		// positions that no longer hold. Dropping the tickets means those answers are ignored when
		// they arrive.
		for (auto it = m_PendingCounts.begin(); it != m_PendingCounts.end();)
		{
			if (it->second.Path == path)
			{
				m_Server.Cancel(it->first);
				it = m_PendingCounts.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	void LspAnswerRouter::AskDefinition(DefinitionQuestion question)
	{
		// A second F12 before the first has answered supersedes it: the user is asking a new
		// question, and an answer to the old one arriving late would be about a place they have
		// since moved past.
		if (m_PendingDefinition)
		{
			m_Server.Cancel(m_PendingDefinition->Ticket);
		}

		const LanguageId language = LanguageOfPath(question.Path);

		m_Server.EnsureStarted(question.Path, language);

		const LanguageServices::Ticket ticket =
			m_Server.AskDefinition(question.Path, language, question.From);

		m_PendingDefinition = PendingDefinition{ticket, std::move(question)};
	}

	void LspAnswerRouter::AskReferences(const std::filesystem::path& path,
										const FilePosition& where, std::string symbol)
	{
		// A second question before the first has answered supersedes it, on the same reasoning as
		// AskDefinition: the user is asking about something else now, and a late answer to the
		// old question would be about a symbol they have since moved past.
		if (m_PendingReferences)
		{
			m_Server.Cancel(m_PendingReferences->Ticket);
		}

		const LanguageId language = LanguageOfPath(path);

		m_Server.EnsureStarted(path, language);

		const LanguageServices::Ticket ticket = m_Server.AskReferences(path, language, where);

		m_PendingReferences = PendingReferences{ticket, std::move(symbol)};
	}

	void LspAnswerRouter::AskSwitchSourceHeader(const std::filesystem::path& path)
	{
		if (m_PendingSwitch)
		{
			m_Server.Cancel(m_PendingSwitch->Ticket);
		}

		const LanguageId language = LanguageOfPath(path);

		m_Server.EnsureStarted(path, language);

		m_PendingSwitch = PendingSwitch{m_Server.AskSwitchSourceHeader(path, language), path};
	}

	void LspAnswerRouter::AskHover(HoverQuestion question)
	{
		if (m_PendingHover)
		{
			m_Server.Cancel(m_PendingHover->Ticket);
		}

		const LanguageId language = LanguageOfPath(question.Path);

		m_Server.EnsureStarted(question.Path, language);

		const LanguageServices::Ticket ticket =
			m_Server.AskHover(question.Path, language, question.From);

		m_PendingHover = PendingHover{ticket, std::move(question)};
	}

	void LspAnswerRouter::AskPreviewDefinition(HoverQuestion question)
	{
		if (m_PendingPreview)
		{
			m_Server.Cancel(m_PendingPreview->Ticket);
		}

		const LanguageId language = LanguageOfPath(question.Path);

		m_Server.EnsureStarted(question.Path, language);

		const LanguageServices::Ticket ticket =
			m_Server.AskDefinition(question.Path, language, question.From);

		m_PendingPreview = PendingHover{ticket, std::move(question)};
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
	}

	bool LspAnswerRouter::IsAnalysing(const std::filesystem::path& path) const
	{
		if (const auto found = m_Schedules.find(path); found != m_Schedules.end())
		{
			// Wanted but not yet asked counts too: the retry clock is a delay before asking, not
			// an answer, and a bar that went quiet during it would be saying the file had settled.
			if (found->second.Outstanding || found->second.Want)
			{
				return true;
			}
		}

		for (const auto& [ticket, pending] : m_PendingSemantic)
		{
			if (pending == path)
			{
				return true;
			}
		}

		for (const auto& [ticket, pending] : m_PendingSymbols)
		{
			if (pending == path)
			{
				return true;
			}
		}

		return false;
	}

	void LspAnswerRouter::Collect(LspAnswerTarget& target)
	{
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
				target.ApplyReferences(pending.Symbol, answer);
				continue;
			}

			if (m_PendingSwitch && m_PendingSwitch->Ticket == answer.For)
			{
				const PendingSwitch pending = std::move(*m_PendingSwitch);

				m_PendingSwitch.reset();
				target.ApplySwitchSourceHeader(pending.From, answer);
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

			if (const auto colours = m_PendingSemantic.find(answer.For);
				colours != m_PendingSemantic.end())
			{
				const std::filesystem::path path = colours->second;

				// Taken out whether or not the answer failed: a failed ticket left here would hold
				// IsAnalysing true for as long as the tab stayed open.
				m_PendingSemantic.erase(colours);

				if (!answer.Failed)
				{
					target.ApplySemanticTokens(path, answer);
				}

				continue;
			}

			if (const auto symbols = m_PendingSymbols.find(answer.For);
				symbols != m_PendingSymbols.end())
			{
				const std::filesystem::path path = symbols->second;

				m_PendingSymbols.erase(symbols);

				if (const auto schedule = m_Schedules.find(path); schedule != m_Schedules.end())
				{
					// A restart lets a new question go out while the dead client's is still
					// unanswered, so this answer may be the older of two. Freeing the schedule
					// while the newer one is in flight would have the retry clock send a third.
					schedule->second.Outstanding =
						std::any_of(m_PendingSymbols.begin(), m_PendingSymbols.end(),
									[&path](const auto& entry) { return entry.second == path; });

					// A failure says nothing about whether the next attempt would succeed - a
					// server that was still indexing answers this way - so Want is left standing
					// and the retry clock set in AskSymbolsIfDue decides when to try again.
					if (!answer.Failed && target.ApplySymbols(path, answer))
					{
						schedule->second.Want = false;
					}
				}

				continue;
			}

			// The preview panes and the background pass ask questions of their own, on their own
			// tickets, and are offered them here because this is the only drain. Answered by
			// return value so an answer nobody claims still falls through to the counts below.
			if (target.Offer(answer))
			{
				continue;
			}

			const auto count = m_PendingCounts.find(answer.For);

			if (count == m_PendingCounts.end())
			{
				// Nothing here asked this, or what asked it has since been dropped - an edit
				// invalidated the row, or the document closed. Either way the answer is about a
				// question that no longer stands.
				continue;
			}

			const PendingCount pending = count->second;

			m_PendingCounts.erase(count);

			if (answer.Failed)
			{
				// Left in the caller's asked rows deliberately: a failure that is re-asked the
				// moment it comes back is a request per frame for as long as the server keeps
				// failing. The next edit or reply clears them and gives the row another chance.
				continue;
			}

			target.ApplyCount(pending.Path, pending.DocumentRow, answer.Locations.size());
		}
	}
}
