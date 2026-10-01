#include "Git/GitModel.h"

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

			PumpWrites();
			return;
		}

		m_WriteError.reset();

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
