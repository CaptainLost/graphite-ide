#include "Git/GitModel.h"

#include "Core/App/Redraw.h"
#include "Core/Base/Ascii.h"
#include "Core/FileSystem/FileSystem.h"

#include "Git/CheckoutQuestion.h"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <exception>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Graphite {

	namespace {

		// WARNING: No default, so a kind added later is a warning here, and warnings fail the build.
		bool IsAnswerRatherThanWrite(GitModel::WriteKind kind)
		{
			switch (kind)
			{
			case GitModel::WriteKind::DeleteCheck:
			case GitModel::WriteKind::CheckoutPlan:
				return true;

			case GitModel::WriteKind::Stage:
			case GitModel::WriteKind::Unstage:
			case GitModel::WriteKind::Discard:
			case GitModel::WriteKind::Commit:
			case GitModel::WriteKind::Init:
			case GitModel::WriteKind::CreateBranch:
			case GitModel::WriteKind::RenameBranch:
			case GitModel::WriteKind::DeleteBranch:
			case GitModel::WriteKind::StashSave:
			case GitModel::WriteKind::StashApply:
			case GitModel::WriteKind::StashPop:
			case GitModel::WriteKind::StashDrop:
			case GitModel::WriteKind::Checkout:
				break;
			}

			return false;
		}

		// WARNING: No default, so a kind added later is a warning here, and warnings fail the build.
		bool ChangesWorkingTree(GitModel::WriteKind kind)
		{
			switch (kind)
			{
			case GitModel::WriteKind::Stage:
			case GitModel::WriteKind::Unstage:
			case GitModel::WriteKind::Discard:
			case GitModel::WriteKind::Commit:
			case GitModel::WriteKind::Init:
			case GitModel::WriteKind::StashSave:
			case GitModel::WriteKind::StashApply:
			case GitModel::WriteKind::StashPop:
			case GitModel::WriteKind::Checkout:
				return true;

			case GitModel::WriteKind::CreateBranch:
			case GitModel::WriteKind::RenameBranch:
			case GitModel::WriteKind::DeleteBranch:
			case GitModel::WriteKind::StashDrop:
			case GitModel::WriteKind::DeleteCheck:
			case GitModel::WriteKind::CheckoutPlan:
				break;
			}

			return false;
		}

		// A status entry for an untracked folder may end in a separator, which leaves its filename empty.
		std::string LastPartOf(const std::filesystem::path& path)
		{
			const std::filesystem::path name = path.filename();

			return Core::ToUtf8(name.empty() ? path.parent_path().filename() : name);
		}

		void NamePaths(GitModel::WriteSubject& subject, const std::vector<std::filesystem::path>& paths)
		{
			subject.PathCount = paths.size();

			if (paths.size() == 1)
			{
				subject.OnePathName = LastPartOf(paths.front());
			}
		}

		std::vector<std::filesystem::path> PathsOf(const DiscardRequest& request)
		{
			std::vector<std::filesystem::path> paths = request.Restore;
			paths.insert(paths.end(), request.Delete.begin(), request.Delete.end());

			return paths;
		}

		std::string FirstLineOf(std::string_view message)
		{
			const std::string trimmed = Core::Trimmed(message);

			return Core::Trimmed(std::string_view(trimmed).substr(0, trimmed.find('\n')));
		}

		Core::UntrackedFiles UntrackedFilesFor(bool includeUntracked)
		{
			return includeUntracked ? Core::UntrackedFiles::Included : Core::UntrackedFiles::Excluded;
		}

		Core::Error NothingToStashError(bool untrackedFilesIncluded)
		{
			return Core::Error{
				Core::ErrorCode::NotFound,
				untrackedFilesIncluded
					? "Nothing to stash"
					: "Nothing to stash: untracked files are stashed only with Include untracked ticked"};
		}

		bool ContainsPath(const std::vector<std::filesystem::path>& paths, const std::filesystem::path& wanted)
		{
			for (const std::filesystem::path& path : paths)
			{
				if (path == wanted)
				{
					return true;
				}
			}

			return false;
		}
	}

	void GitModel::RequestStage(std::vector<std::filesystem::path> paths)
	{
		Enqueue({.Kind = WriteKind::Stage, .Paths = std::move(paths)});
	}

	void GitModel::RequestUnstage(std::vector<std::filesystem::path> paths)
	{
		Enqueue({.Kind = WriteKind::Unstage, .Paths = std::move(paths)});
	}

	void GitModel::RequestDiscard(DiscardRequest request)
	{
		Enqueue({.Kind = WriteKind::Discard, .Discard = std::move(request)});
	}

	void GitModel::RequestCommit(Core::CommitRequest request)
	{
		Enqueue({.Kind = WriteKind::Commit, .Commit = std::move(request)});
	}

	void GitModel::RequestInit()
	{
		Enqueue({.Kind = WriteKind::Init});
	}

	void GitModel::RequestCreateBranch(std::string name, Core::Oid from)
	{
		Enqueue({.Kind = WriteKind::CreateBranch, .Name = std::move(name), .Id = from});
	}

	void GitModel::RequestRenameBranch(std::string from, std::string to)
	{
		Enqueue({.Kind = WriteKind::RenameBranch, .Name = std::move(from), .OtherName = std::move(to)});
	}

	void GitModel::RequestDeleteCheck(std::string branch)
	{
		Enqueue({.Kind = WriteKind::DeleteCheck, .Name = std::move(branch)});
	}

	void GitModel::RequestDeleteBranch(std::string branch)
	{
		Enqueue({.Kind = WriteKind::DeleteBranch, .Name = std::move(branch)});
	}

	void GitModel::RequestStashSave(std::string message, bool includeUntracked)
	{
		Enqueue({.Kind = WriteKind::StashSave, .Name = std::move(message), .Flag = includeUntracked});
	}

	void GitModel::RequestStashApply(Core::Oid id)
	{
		Enqueue({.Kind = WriteKind::StashApply, .Id = id});
	}

	void GitModel::RequestStashPop(Core::Oid id)
	{
		Enqueue({.Kind = WriteKind::StashPop, .Id = id});
	}

	void GitModel::RequestStashDrop(Core::Oid id)
	{
		Enqueue({.Kind = WriteKind::StashDrop, .Id = id});
	}

	void GitModel::RequestCheckoutPlan(Core::CheckoutTarget target)
	{
		Enqueue({.Kind = WriteKind::CheckoutPlan, .Target = std::move(target)});
	}

	void GitModel::RequestCheckout(CheckoutRequest request)
	{
		Enqueue({.Kind = WriteKind::Checkout, .Checkout = std::move(request)});
	}

	std::optional<DeleteCheck> GitModel::TakeDeleteCheck()
	{
		std::optional<DeleteCheck> taken = std::move(m_DeleteCheck);
		m_DeleteCheck.reset();
		return taken;
	}

	std::optional<CheckoutPlanAnswer> GitModel::TakeCheckoutPlan()
	{
		std::optional<CheckoutPlanAnswer> taken = std::move(m_CheckoutPlan);
		m_CheckoutPlan.reset();
		return taken;
	}

	std::vector<std::filesystem::path> GitModel::TakeCheckedOut()
	{
		std::vector<std::filesystem::path> taken = std::move(m_CheckedOut);
		m_CheckedOut.clear();
		return taken;
	}

	std::optional<Core::Oid> GitModel::TakeCommitted()
	{
		std::optional<Core::Oid> taken = m_Committed;
		m_Committed.reset();
		return taken;
	}

	std::optional<GitModel::WriteFailure> GitModel::TakeWriteError()
	{
		if (m_WriteErrors.empty())
		{
			return std::nullopt;
		}

		WriteFailure taken = std::move(m_WriteErrors.front());
		m_WriteErrors.pop_front();
		return taken;
	}

	std::vector<std::filesystem::path> GitModel::TakeDiscarded()
	{
		std::vector<std::filesystem::path> taken = std::move(m_Discarded);
		m_Discarded.clear();
		return taken;
	}

	void GitModel::Enqueue(WriteJob job)
	{
		const bool hasFolderOpen = !m_Root.empty();

		if (!hasFolderOpen)
		{
			return;
		}

		job.Generation = m_WriteGeneration;

		if (!IsAnswerRatherThanWrite(job.Kind))
		{
			job.Ticket = ++m_LastTicket;

			const std::optional<Core::CheckoutTarget> checkout =
				job.Kind == WriteKind::Checkout ? std::optional<Core::CheckoutTarget>{job.Checkout.Target} : std::nullopt;

			m_Tickets.push_back(WriteTicket{.Number = job.Ticket, .Subject = DescribeWrite(job), .Checkout = checkout});
			m_Confirmed.reset();
		}

		m_WriteQueue.push_back(std::move(job));
		PumpWrites();
	}

	GitModel::WriteSubject GitModel::DescribeWrite(const WriteJob& job) const
	{
		WriteSubject subject{.Kind = job.Kind};

		switch (job.Kind)
		{
		case WriteKind::Stage:
		case WriteKind::Unstage:
			NamePaths(subject, job.Paths);
			break;

		case WriteKind::Discard:
			NamePaths(subject, PathsOf(job.Discard));
			break;

		case WriteKind::Commit:
			subject.CommitSummary = FirstLineOf(job.Commit.Message);
			break;

		case WriteKind::CreateBranch:
		case WriteKind::DeleteBranch:
		case WriteKind::DeleteCheck:
			subject.Name = job.Name;
			break;

		case WriteKind::RenameBranch:
			subject.Name = job.Name;
			subject.NewName = job.OtherName;
			break;

		case WriteKind::StashApply:
		case WriteKind::StashPop:
		case WriteKind::StashDrop:
			subject.StashIndex = StashIndexOf(job.Id);
			break;

		case WriteKind::Checkout:
			subject.Name = CheckoutTargetLabel(job.Checkout.Target);
			break;

		case WriteKind::CheckoutPlan:
			subject.Name = CheckoutTargetLabel(*job.Target);
			break;

		case WriteKind::Init:
		case WriteKind::StashSave:
			break;
		}

		return subject;
	}

	std::optional<std::size_t> GitModel::StashIndexOf(const Core::Oid& stash) const
	{
		const Core::Ref<const std::vector<Core::StashEntry>> stashes = m_Stashes.Get();

		if (stashes == nullptr)
		{
			return std::nullopt;
		}

		for (const Core::StashEntry& entry : *stashes)
		{
			if (entry.Id == stash)
			{
				return entry.Index;
			}
		}

		return std::nullopt;
	}

	GitModel::WriteTicket* GitModel::FindTicket(std::uint64_t number)
	{
		for (WriteTicket& ticket : m_Tickets)
		{
			if (ticket.Number == number)
			{
				return &ticket;
			}
		}

		return nullptr;
	}

	const GitModel::WriteTicket* GitModel::RunningTicket() const
	{
		for (const WriteTicket& ticket : m_Tickets)
		{
			if (ticket.Phase == TicketPhase::Running)
			{
				return &ticket;
			}
		}

		return nullptr;
	}

	const GitModel::WriteTicket* GitModel::NewestUnfailedTicket() const
	{
		const WriteTicket* newest = nullptr;

		for (const WriteTicket& ticket : m_Tickets)
		{
			if (!ticket.Failed)
			{
				newest = &ticket;
			}
		}

		return newest;
	}

	void GitModel::ForgetTicket(std::uint64_t number)
	{
		for (std::size_t ticketIndex = 0; ticketIndex < m_Tickets.size(); ++ticketIndex)
		{
			if (m_Tickets[ticketIndex].Number == number)
			{
				m_Tickets.erase(m_Tickets.begin() + static_cast<std::ptrdiff_t>(ticketIndex));
				return;
			}
		}
	}

	void GitModel::MarkTicketRunning(std::uint64_t number)
	{
		if (WriteTicket* ticket = FindTicket(number); ticket != nullptr)
		{
			ticket->Phase = TicketPhase::Running;
		}
	}

	void GitModel::AwaitStatusAfter(std::uint64_t number, const std::optional<Core::Oid>& committed)
	{
		WriteTicket* ticket = FindTicket(number);

		if (ticket == nullptr)
		{
			return;
		}

		ticket->Phase = TicketPhase::Applied;
		ticket->StatusAskedWhenApplied = m_Status.GetRequested();
		ticket->HeadAskedWhenApplied = m_Head.GetRequested();
		ticket->Subject.Committed = committed;
	}

	void GitModel::FailTicket(std::uint64_t number, bool changedRepository)
	{
		WriteTicket* ticket = FindTicket(number);

		if (ticket == nullptr)
		{
			return;
		}

		if (!changedRepository)
		{
			ForgetTicket(number);
			return;
		}

		const std::optional<Core::Oid> nothingCommitted;
		AwaitStatusAfter(number, nothingCommitted);
		ticket->Failed = true;
	}

	bool GitModel::HasSettled(const WriteTicket& ticket) const
	{
		return ticket.Phase == TicketPhase::Applied && m_Status.GetApplied() > ticket.StatusAskedWhenApplied &&
			   m_Head.GetApplied() > ticket.HeadAskedWhenApplied;
	}

	void GitModel::SettleTicketsWhoseStatusLanded()
	{
		std::optional<WriteSubject> newestSettled;
		std::deque<WriteTicket> unsettled;

		for (WriteTicket& ticket : m_Tickets)
		{
			if (!HasSettled(ticket))
			{
				unsettled.push_back(std::move(ticket));
			}
			else if (!ticket.Failed)
			{
				newestSettled = std::move(ticket.Subject);
			}
		}

		m_Tickets = std::move(unsettled);

		// WARNING: Tickets settle in the order they were asked for, so any left are newer, and the report is theirs.
		if (!newestSettled.has_value() || NewestUnfailedTicket() != nullptr)
		{
			return;
		}

		m_Confirmed = std::move(newestSettled);
		m_ConfirmedSecondsLeft = kWriteConfirmationSeconds;
		Core::Redraw::RequestIn(std::chrono::duration<double>(m_ConfirmedSecondsLeft));
	}

	void GitModel::CountDownConfirmation(Core::Timestep timestep)
	{
		if (!m_Confirmed.has_value())
		{
			return;
		}

		m_ConfirmedSecondsLeft -= timestep.GetSeconds();

		if (m_ConfirmedSecondsLeft <= 0.0f)
		{
			m_Confirmed.reset();
			return;
		}

		Core::Redraw::RequestIn(std::chrono::duration<double>(m_ConfirmedSecondsLeft));
	}

	std::optional<GitModel::WriteReport> GitModel::GetWriteReport() const
	{
		const WriteTicket* reported = RunningTicket();

		if (reported == nullptr)
		{
			reported = NewestUnfailedTicket();
		}

		if (reported != nullptr)
		{
			return WriteReport{.Subject = reported->Subject, .Progress = WriteProgress::Running};
		}

		if (m_Confirmed.has_value())
		{
			return WriteReport{.Subject = *m_Confirmed, .Progress = WriteProgress::Done};
		}

		return std::nullopt;
	}

	bool GitModel::IsWriteUnsettled() const
	{
		return m_WriteInFlight || !m_Tickets.empty();
	}

	bool GitModel::IsUpdatingWorkingTree() const
	{
		for (const WriteTicket& ticket : m_Tickets)
		{
			if (ChangesWorkingTree(ticket.Subject.Kind))
			{
				return true;
			}
		}

		return false;
	}

	std::optional<Core::CheckoutTarget> GitModel::GetCheckoutUnderWay() const
	{
		for (const WriteTicket& ticket : m_Tickets)
		{
			if (ticket.Checkout.has_value() && !ticket.Failed)
			{
				return ticket.Checkout;
			}
		}

		return std::nullopt;
	}

	void GitModel::PumpWrites()
	{
		if (m_WriteInFlight || m_WriteQueue.empty())
		{
			return;
		}

		WriteJob job = std::move(m_WriteQueue.front());
		m_WriteQueue.pop_front();
		m_WriteInFlight = true;

		MarkTicketRunning(job.Ticket);

		m_SubmitWrite([this, job = std::move(job)]() mutable {
			WriteOutcome outcome = RunWrite(job);

			Deliver([this, job = std::move(job), outcome = std::move(outcome)]() mutable {
				ApplyWrite(job, std::move(outcome));
			});
		});
	}

	GitModel::WriteOutcome GitModel::RunWrite(const WriteJob& job)
	{
		// WARNING: JobQueue abandons a job that throws, so a write that threw would never reach
		// Deliver and m_WriteInFlight, which only ApplyWrite clears, would stay true forever.
		// Every path through m_Port must end in an outcome, never an escaping exception.
		WriteOutcome outcome;

		try
		{
			switch (job.Kind)
			{
			case WriteKind::Stage:
			{
				if (const Core::Result<void> result = m_Port.Stage(job.Paths); !result.HasValue())
				{
					outcome.Failure = result.GetError();
				}

				break;
			}

			case WriteKind::Unstage:
			{
				if (const Core::Result<void> result = m_Port.Unstage(job.Paths); !result.HasValue())
				{
					outcome.Failure = result.GetError();
				}

				break;
			}

			case WriteKind::Discard:
			{
				// WARNING: Restoring comes first so that a failed delete leaves the tracked
				// files already put back.
				if (!job.Discard.Restore.empty())
				{
					if (const Core::Result<void> result = m_Port.DiscardWorkTree(job.Discard.Restore); !result.HasValue())
					{
						outcome.Failure = result.GetError();
						break;
					}

					outcome.Changed = job.Discard.Restore;
				}

				if (!job.Discard.Delete.empty())
				{
					if (const Core::Result<void> result = m_Port.DeleteUntracked(job.Discard.Delete); !result.HasValue())
					{
						outcome.Failure = result.GetError();
						break;
					}

					outcome.Changed.insert(
						outcome.Changed.end(), job.Discard.Delete.begin(), job.Discard.Delete.end());
				}

				break;
			}

			case WriteKind::Commit:
			{
				const Core::Result<Core::Oid> result = m_Port.Commit(job.Commit);

				if (result.HasValue())
				{
					outcome.Committed = result.GetValue();
				}
				else
				{
					outcome.Failure = result.GetError();
				}

				break;
			}

			case WriteKind::Init:
			{
				if (const Core::Result<void> result = m_Port.Init(); !result.HasValue())
				{
					outcome.Failure = result.GetError();
				}

				break;
			}

			case WriteKind::CreateBranch:
			{
				if (const Core::Result<void> result = m_Port.CreateBranch(job.Name, job.Id); !result.HasValue())
				{
					outcome.Failure = result.GetError();
				}

				break;
			}

			case WriteKind::RenameBranch:
			{
				if (const Core::Result<void> result = m_Port.RenameBranch(job.Name, job.OtherName); !result.HasValue())
				{
					outcome.Failure = result.GetError();
				}

				break;
			}

			case WriteKind::DeleteCheck:
			{
				const Core::Result<std::size_t> unmergedCount = m_Port.CountUnmergedCommits(job.Name);

				if (unmergedCount.HasValue())
				{
					outcome.Unmerged = unmergedCount.GetValue();
				}
				else
				{
					outcome.Failure = unmergedCount.GetError();
				}

				break;
			}

			case WriteKind::DeleteBranch:
			{
				if (const Core::Result<void> result = m_Port.DeleteBranch(job.Name); !result.HasValue())
				{
					outcome.Failure = result.GetError();
				}

				break;
			}

			case WriteKind::StashSave:
			{
				const Core::Result<bool> result = m_Port.StashSave(job.Name, UntrackedFilesFor(job.Flag));

				if (!result.HasValue())
				{
					outcome.Failure = result.GetError();
				}
				else if (!result.GetValue())
				{
					outcome.Failure = NothingToStashError(job.Flag);
				}

				break;
			}

			case WriteKind::StashApply:
			{
				if (const Core::Result<void> result = m_Port.StashApply(job.Id); !result.HasValue())
				{
					outcome.Failure = result.GetError();
				}

				break;
			}

			case WriteKind::StashPop:
			{
				if (const Core::Result<void> result = m_Port.StashPop(job.Id); !result.HasValue())
				{
					outcome.Failure = result.GetError();
				}

				break;
			}

			case WriteKind::StashDrop:
			{
				if (const Core::Result<void> result = m_Port.StashDrop(job.Id); !result.HasValue())
				{
					outcome.Failure = result.GetError();
				}

				break;
			}

			case WriteKind::CheckoutPlan:
			{
				Core::Result<Core::CheckoutPlan> plan = m_Port.PlanCheckout(*job.Target);

				if (plan.HasValue())
				{
					outcome.Plan = std::move(plan).GetValue();
				}
				else
				{
					outcome.Failure = plan.GetError();
				}

				break;
			}

			case WriteKind::Checkout:
			{
				outcome = RunCheckout(job.Checkout);
				break;
			}
			}
		}
		catch (const std::exception& exception)
		{
			outcome.Failure = Core::Error{
				Core::ErrorCode::IoFailure,
				std::string{"a write threw: "} + exception.what()};
		}
		catch (...)
		{
			outcome.Failure = Core::Error{Core::ErrorCode::IoFailure, "a write threw"};
		}

		return outcome;
	}

	GitModel::WriteOutcome GitModel::RunCheckout(const CheckoutRequest& request)
	{
		WriteOutcome outcome;

		const std::string failurePrefix = "Could not check out " + CheckoutTargetLabel(request.Target) + ": ";
		std::string preparedNote;

		if (request.Preparation != CheckoutPreparation::None)
		{
			// WARNING: The files are noted before the preparation rewrites them so that the editor
			// hears about them as well as about the checkout's own. An unreadable status only
			// costs that notice and must not refuse a checkout the user confirmed.
			if (const Core::Result<Core::WorkingTreeStatus> status = m_Port.QueryStatus(); status.HasValue())
			{
				for (const Core::FileStatus& entry : status.GetValue().Entries)
				{
					const bool preparationRewritesEntry = !entry.IsUntracked || request.IncludeUntracked;

					if (preparationRewritesEntry)
					{
						outcome.Changed.push_back(entry.Path);
					}
				}
			}

			if (request.Preparation == CheckoutPreparation::Discard)
			{
				if (const Core::Result<void> discardResult = m_Port.DiscardAll(UntrackedFilesFor(request.IncludeUntracked));
					!discardResult.HasValue())
				{
					outcome.Failure = discardResult.GetError();
				}
				else
				{
					preparedNote = " Your changes were discarded.";
				}
			}
			else
			{
				const Core::Result<bool> stashResult =
					m_Port.StashSave(request.StashMessage, UntrackedFilesFor(request.IncludeUntracked));

				if (!stashResult.HasValue())
				{
					outcome.Failure = stashResult.GetError();
				}
				else
				{
					const bool anythingStashed = stashResult.GetValue();

					if (anythingStashed)
					{
						preparedNote = " Your changes were stashed as stash@{0}.";
					}
				}
			}

			if (outcome.Failure.has_value())
			{
				outcome.Failure->Message = failurePrefix + outcome.Failure->Message;
				outcome.Changed.clear();
				return outcome;
			}
		}

		// WARNING: A checkout that fails keeps the preparation's paths, because those files did
		// change, and adds every path it may have changed itself before failing partway.
		Core::CheckoutOutcome checkout = m_Port.Checkout(request.Target);

		for (std::filesystem::path& path : checkout.Changed)
		{
			if (!ContainsPath(outcome.Changed, path))
			{
				outcome.Changed.push_back(std::move(path));
			}
		}

		outcome.Failure = std::move(checkout.Failure);

		if (outcome.Failure.has_value())
		{
			std::string message = failurePrefix + outcome.Failure->Message;

			if (!preparedNote.empty() && !message.ends_with('.'))
			{
				message += '.';
			}

			outcome.Failure->Message = message + preparedNote;
		}

		return outcome;
	}

	void GitModel::ApplyWrite(const WriteJob& job, WriteOutcome outcome)
	{
		m_WriteInFlight = false;

		const bool jobWasForClosedFolder = job.Generation != m_WriteGeneration;

		if (jobWasForClosedFolder)
		{
			PumpWrites();
			return;
		}

		// WARNING: CheckoutPlan and DeleteCheck are answers, not writes. They neither refresh the
		// model nor count as applied, and they leave m_WriteErrors alone: a plan landing after a
		// failed stash must not erase that failure, and the dialog waiting on the answer must
		// tell its own failure from an earlier write's.
		if (job.Kind == WriteKind::CheckoutPlan)
		{
			m_CheckoutPlan = CheckoutPlanAnswer{*job.Target, std::move(outcome.Plan), std::move(outcome.Failure)};
			PumpWrites();
			return;
		}

		if (job.Kind == WriteKind::DeleteCheck)
		{
			const std::size_t noUnmergedCommits = 0;
			m_DeleteCheck =
				DeleteCheck{job.Name, outcome.Unmerged.value_or(noUnmergedCommits), std::move(outcome.Failure)};
			PumpWrites();
			return;
		}

		if (outcome.Failure.has_value())
		{
			if (job.Kind == WriteKind::Init)
			{
				m_InitError = std::move(outcome.Failure);
			}
			else
			{
				m_WriteErrors.push_back(WriteFailure{job.Kind, std::move(*outcome.Failure)});
			}

			const bool mayHaveWrittenSomeFiles =
				job.Kind == WriteKind::StashApply || job.Kind == WriteKind::StashPop;

			if (mayHaveWrittenSomeFiles)
			{
				++m_AppliedWrites;
				MarkDirty();
			}

			if (job.Kind == WriteKind::Discard)
			{
				if (!outcome.Changed.empty())
				{
					m_Discarded = std::move(outcome.Changed);
					++m_AppliedWrites;
				}

				MarkDirty();
			}

			if (job.Kind == WriteKind::Checkout)
			{
				if (!outcome.Changed.empty())
				{
					m_CheckedOut = std::move(outcome.Changed);
					++m_AppliedWrites;
				}

				MarkDirty();
			}

			const bool refreshesAfterFailure =
				mayHaveWrittenSomeFiles || job.Kind == WriteKind::Discard || job.Kind == WriteKind::Checkout;
			FailTicket(job.Ticket, refreshesAfterFailure);

			if (refreshesAfterFailure)
			{
				ReadStatusAfterWrite();
			}

			PumpWrites();
			return;
		}

		AwaitStatusAfter(job.Ticket, outcome.Committed);

		if (job.Kind == WriteKind::Checkout)
		{
			m_CheckedOut = std::move(outcome.Changed);
		}

		if (outcome.Committed.has_value())
		{
			m_Committed = outcome.Committed;
		}

		if (job.Kind == WriteKind::Discard)
		{
			// WARNING: Replaced rather than appended, which is safe only because at most one write
			// is applied per OnUpdate: a second discard cannot land before TakeDiscarded has read
			// this one. The same holds for m_CheckedOut.
			m_Discarded = std::move(outcome.Changed);
		}

		// WARNING: Marked here rather than left to the watcher because the tests run without
		// one and the watcher's allowlist can be missing something.
		MarkDirty();

		if (job.Kind == WriteKind::Init)
		{
			m_InitSettlesAfter = m_Status.GetRequested();
			m_InitError.reset();
		}

		++m_AppliedWrites;

		// WARNING: Asked after the settling marks above are taken, so this read is one that settles them.
		ReadStatusAfterWrite();

		PumpWrites();
	}
}
