#include "Git/GitModel.h"

#include "Git/CheckoutQuestion.h"

#include <cstddef>
#include <filesystem>
#include <exception>
#include <string>
#include <utility>
#include <vector>

namespace Graphite {

	namespace {

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
		m_WriteQueue.push_back(std::move(job));
		PumpWrites();
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

		m_Submit([this, job = std::move(job)]() mutable {
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

			PumpWrites();
			return;
		}

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

		PumpWrites();
	}
}
