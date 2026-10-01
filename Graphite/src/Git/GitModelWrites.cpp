#include "Git/GitModel.h"

#include "Git/CheckoutQuestion.h"

#include <algorithm>
#include <exception>
#include <string>
#include <utility>

namespace Graphite {

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

	std::vector<std::filesystem::path> GitModel::TakeDiscarded()
	{
		std::vector<std::filesystem::path> taken = std::move(m_Discarded);
		m_Discarded.clear();
		return taken;
	}

	void GitModel::Enqueue(WriteJob job)
	{
		// With no folder open there is no repository to write to, and the port would answer
		// NotFound for every one of these — an error the user never asked to see.
		if (m_Root.empty())
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
		WriteOutcome outcome;

		// JobQueue catches a job that throws and abandons it - see JobQueue.cpp - which for
		// a query is harmless because the next debounce re-asks, but for a write would never
		// reach Deliver at all. Without this try/catch, m_WriteInFlight would stay true
		// forever: ApplyWrite is the only place that clears it, and an abandoned job never
		// gets there. Every path through m_Port must therefore end in an outcome, never an
		// escaping exception.
		try
		{
			switch (job.Kind)
			{
			case WriteKind::Stage:
			{
				if (const auto result = m_Port.Stage(job.Paths); !result.HasValue())
				{
					outcome.Failure = result.GetError();
				}

				break;
			}

			case WriteKind::Unstage:
			{
				if (const auto result = m_Port.Unstage(job.Paths); !result.HasValue())
				{
					outcome.Failure = result.GetError();
				}

				break;
			}

			case WriteKind::Discard:
			{
				// Restoring first, so a failure to delete leaves the tracked files already put
				// back rather than leaving both halves half-done in an order nobody chose.
				if (!job.Discard.Restore.empty())
				{
					if (const auto result = m_Port.DiscardWorkTree(job.Discard.Restore); !result.HasValue())
					{
						outcome.Failure = result.GetError();
						break;
					}

					outcome.Changed = job.Discard.Restore;
				}

				if (!job.Discard.Delete.empty())
				{
					if (const auto result = m_Port.DeleteUntracked(job.Discard.Delete); !result.HasValue())
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
				auto result = m_Port.Commit(job.Commit);

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
				if (const auto result = m_Port.Init(); !result.HasValue())
				{
					outcome.Failure = result.GetError();
				}

				break;
			}

			case WriteKind::CreateBranch:
			{
				if (const auto result = m_Port.CreateBranch(job.Name, job.Id); !result.HasValue())
				{
					outcome.Failure = result.GetError();
				}

				break;
			}

			case WriteKind::RenameBranch:
			{
				if (const auto result = m_Port.RenameBranch(job.Name, job.OtherName); !result.HasValue())
				{
					outcome.Failure = result.GetError();
				}

				break;
			}

			case WriteKind::DeleteCheck:
			{
				auto count = m_Port.CountUnmergedCommits(job.Name);

				if (count.HasValue())
				{
					outcome.Unmerged = count.GetValue();
				}
				else
				{
					outcome.Failure = count.GetError();
				}

				break;
			}

			case WriteKind::DeleteBranch:
			{
				if (const auto result = m_Port.DeleteBranch(job.Name); !result.HasValue())
				{
					outcome.Failure = result.GetError();
				}

				break;
			}

			case WriteKind::StashSave:
			{
				const auto result = m_Port.StashSave(job.Name, job.Flag);

				if (!result.HasValue())
				{
					outcome.Failure = result.GetError();
				}
				else if (!result.GetValue())
				{
					// Said rather than passed off as done: the dialog closed on the click, and
					// a tree left exactly as it was would otherwise be the only answer. Left
					// out, untracked files are the likely reason.
					outcome.Failure = Core::Error{
						Core::ErrorCode::NotFound,
						job.Flag ? "Nothing to stash"
								 : "Nothing to stash: untracked files are stashed only with Include untracked ticked"};
				}

				break;
			}

			case WriteKind::StashApply:
			{
				if (const auto result = m_Port.StashApply(job.Id); !result.HasValue())
				{
					outcome.Failure = result.GetError();
				}

				break;
			}

			case WriteKind::StashPop:
			{
				if (const auto result = m_Port.StashPop(job.Id); !result.HasValue())
				{
					outcome.Failure = result.GetError();
				}

				break;
			}

			case WriteKind::StashDrop:
			{
				if (const auto result = m_Port.StashDrop(job.Id); !result.HasValue())
				{
					outcome.Failure = result.GetError();
				}

				break;
			}

			case WriteKind::CheckoutPlan:
			{
				auto plan = m_Port.PlanCheckout(*job.Target);

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

		// Every failure names the checkout it stopped, since the line reads long after the
		// click; and one that stopped after the preparation says where the changes went.
		const std::string prefix = "Could not check out " + CheckoutTargetLabel(request.Target) + ": ";
		std::string preparedNote;

		if (request.Preparation != CheckoutPreparation::None)
		{
			// Noted before anything moves: these are the files the preparation is about to
			// rewrite under the editor, which has to be told about them as much as about the
			// checkout's own. A status that cannot be read only costs the editor that notice;
			// it is no reason to refuse a checkout the user confirmed.
			if (auto status = m_Port.QueryStatus(); status.HasValue())
			{
				for (const Core::FileStatus& entry : status.GetValue().Entries)
				{
					if (!entry.IsUntracked || request.IncludeUntracked)
					{
						outcome.Changed.push_back(entry.Path);
					}
				}
			}

			if (request.Preparation == CheckoutPreparation::Discard)
			{
				if (const auto result = m_Port.DiscardAll(request.IncludeUntracked); !result.HasValue())
				{
					outcome.Failure = result.GetError();
				}
				else
				{
					preparedNote = " Your changes were discarded.";
				}
			}
			else
			{
				// False - nothing to stash - is not a failure: the checkout has nothing in its
				// way either.
				if (const auto result = m_Port.StashSave(request.StashMessage, request.IncludeUntracked);
					!result.HasValue())
				{
					outcome.Failure = result.GetError();
				}
				else if (result.GetValue())
				{
					preparedNote = " Your changes were stashed as stash@{0}.";
				}
			}

			if (outcome.Failure.has_value())
			{
				// The preparation is what failed, so the checkout never ran and the noted
				// files are not reported as changed.
				outcome.Failure->Message = prefix + outcome.Failure->Message;
				outcome.Changed.clear();
				return outcome;
			}
		}

		// A checkout that fails after a preparation succeeded keeps the preparation's paths:
		// those files did change. And one that fails partway names every path it may have
		// changed itself, which the editor needs just as much.
		Core::CheckoutOutcome checkout = m_Port.Checkout(request.Target);

		for (std::filesystem::path& path : checkout.Changed)
		{
			if (std::find(outcome.Changed.begin(), outcome.Changed.end(), path) == outcome.Changed.end())
			{
				outcome.Changed.push_back(std::move(path));
			}
		}

		outcome.Failure = std::move(checkout.Failure);

		if (outcome.Failure.has_value())
		{
			std::string message = prefix + outcome.Failure->Message;

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

		// The folder this was meant for is gone. Neither its error nor its success has
		// anything to say about the folder that is open now.
		if (job.Generation != m_WriteGeneration)
		{
			PumpWrites();
			return;
		}

		// Answers rather than writes, handled before the write error is looked at. They
		// changed nothing, so they neither refresh the model nor count as applied, and they
		// leave GetWriteError alone either way: a plan landing a frame after a failed stash
		// must not erase that failure before anyone saw it, and the dialog waiting on the
		// answer must be able to tell its own failure from an earlier write's.
		if (job.Kind == WriteKind::CheckoutPlan)
		{
			m_CheckoutPlan = CheckoutPlanAnswer{*job.Target, std::move(outcome.Plan), std::move(outcome.Failure)};
			PumpWrites();
			return;
		}

		if (job.Kind == WriteKind::DeleteCheck)
		{
			m_DeleteCheck = DeleteCheck{job.Name, outcome.Unmerged.value_or(0), std::move(outcome.Failure)};
			PumpWrites();
			return;
		}

		if (outcome.Failure.has_value())
		{
			m_WriteError = std::move(outcome.Failure);

			// An apply or a pop that stopped on a conflict or an I/O error may have written
			// some files already, so the status refreshes as it does for a discard, and the
			// write counts as applied so that what reads the count - the change markers -
			// looks again.
			if (job.Kind == WriteKind::StashApply || job.Kind == WriteKind::StashPop)
			{
				++m_AppliedWrites;
				MarkDirty();
			}

			// A discard that stopped halfway has still changed files the user confirmed
			// discarding: the editor must be told (ADR 0501) and the status must refresh,
			// whatever the error says about the rest.
			if (job.Kind == WriteKind::Discard)
			{
				if (!outcome.Changed.empty())
				{
					m_Discarded = std::move(outcome.Changed);
					++m_AppliedWrites;
				}

				MarkDirty();
			}

			// The same for a checkout: one that failed partway, or after its preparation ran,
			// has still rewritten files under the editor. A preparation that failed may have
			// got partway too, so the refresh runs in every case.
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

		m_WriteError.reset();

		// Replaced rather than appended, for the reason given on m_Discarded below.
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
			// Replaced rather than appended: safe only because at most one write is applied
			// per OnUpdate, so a second discard's Deliver cannot land until this one's inbox
			// entry has already been drained and TakeDiscarded has had its chance to read it.
			m_Discarded = std::move(outcome.Changed);
		}

		// The same mark a file change or a RepositoryChangedEvent makes, so status, branches
		// and history all refresh through one mechanism. Marked here rather than left to the
		// watcher because the tests run without one, and because the watcher's allowlist is
		// a list that can be missing something.
		MarkDirty();

		if (job.Kind == WriteKind::Init)
		{
			m_InitSettlesAfter = m_Status.GetRequested();
		}

		++m_AppliedWrites;

		PumpWrites();
	}
}
