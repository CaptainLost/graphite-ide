#include "Git/GitModel.h"

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

	void GitModel::RequestCreateBranch(
		std::string name, Core::Oid from, std::optional<Core::CheckoutTarget> thenCheckout)
	{
		Enqueue({.Kind = WriteKind::CreateBranch, .Name = std::move(name), .Id = from,
				 .Target = std::move(thenCheckout)});
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

	std::optional<std::pair<Core::CheckoutTarget, Core::CheckoutPlan>> GitModel::TakeCheckoutPlan()
	{
		std::optional<std::pair<Core::CheckoutTarget, Core::CheckoutPlan>> taken = std::move(m_CheckoutPlan);
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
					break;
				}

				if (job.Target.has_value())
				{
					// A checkout that fails partway still names every path it may have
					// changed, so both halves are kept.
					Core::CheckoutOutcome checkout = m_Port.Checkout(*job.Target);
					outcome.Changed = std::move(checkout.Changed);
					outcome.Failure = std::move(checkout.Failure);
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
				if (const auto result = m_Port.StashSave(job.Name, job.Flag); !result.HasValue())
				{
					outcome.Failure = result.GetError();
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
			}

			if (outcome.Failure.has_value())
			{
				// The preparation is what failed, so the checkout never ran and the noted
				// files are not reported as changed.
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

		if (outcome.Failure.has_value())
		{
			m_WriteError = std::move(outcome.Failure);

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
			// has still rewritten files under the editor. A new branch whose checkout then
			// failed exists all the same, so the refresh runs either way.
			if (job.Kind == WriteKind::Checkout || job.Kind == WriteKind::CreateBranch)
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

		// Answers rather than writes: they changed nothing, so they neither refresh the model
		// nor count as applied.
		if (job.Kind == WriteKind::CheckoutPlan)
		{
			m_CheckoutPlan = std::make_pair(*job.Target, std::move(*outcome.Plan));
			PumpWrites();
			return;
		}

		if (job.Kind == WriteKind::DeleteCheck)
		{
			m_DeleteCheck = DeleteCheck{job.Name, *outcome.Unmerged};
			PumpWrites();
			return;
		}

		// Replaced rather than appended, for the reason given on m_Discarded below. A new
		// branch made without a checkout changed no file, and must not wipe an earlier
		// checkout's paths that nobody has taken yet.
		if (job.Kind == WriteKind::Checkout || (job.Kind == WriteKind::CreateBranch && job.Target.has_value()))
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
			m_InitSettlesAfter = m_StatusRequested;
		}

		++m_AppliedWrites;

		PumpWrites();
	}
}
