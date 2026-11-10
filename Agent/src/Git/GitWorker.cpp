#include "corepch.h"

#include "Git/GitWorker.h"

#include <sys/eventfd.h>
#include <unistd.h>

#include <cstdint>
#include <exception>
#include <format>
#include <utility>

namespace Agent {

	GitWorker::GitWorker() : m_Signal(::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC))
	{
		m_Thread = std::thread{[this] { Run(); }};
	}

	GitWorker::~GitWorker()
	{
		{
			std::scoped_lock lock{m_Mutex};
			m_Stopping = true;
		}

		m_Queued.notify_all();
		m_Thread.join();

		if (m_Signal >= 0)
		{
			::close(m_Signal);
		}
	}

	void GitWorker::Submit(Core::AgentRequest request, GitHandler handler)
	{
		{
			std::scoped_lock lock{m_Mutex};
			m_Jobs.push_back(Job{.Request = std::move(request), .Handler = std::move(handler)});
		}

		m_Queued.notify_one();
	}

	std::vector<Core::AgentReply> GitWorker::TakeFinished()
	{
		// Drains the signal first: a reply finished after this read raises it again.
		std::uint64_t pending = 0;
		static_cast<void>(::read(m_Signal, &pending, sizeof pending));

		std::scoped_lock lock{m_Mutex};
		return std::exchange(m_Finished, {});
	}

	void GitWorker::Run()
	{
		while (true)
		{
			Job job;
			{
				std::unique_lock lock{m_Mutex};
				m_Queued.wait(lock, [&] { return m_Stopping || !m_Jobs.empty(); });

				// Graphite has gone (bye or a closed pipe): what is still queued is not run, so a
				// write it already reported as failed does not land behind its back.
				if (m_Stopping || m_Jobs.empty())
				{
					m_Jobs.clear();
					return;
				}

				job = std::move(m_Jobs.front());
				m_Jobs.pop_front();
			}

			Core::AgentReply reply{.Id = job.Request.Id};

			// A handler that throws fails its own request, not the worker.
			try
			{
				Core::Result<GitAnswer> answer = job.Handler(job.Request);

				if (answer)
				{
					reply.Result = std::move(answer.GetValue().Result);
					reply.Payload = std::move(answer.GetValue().Payload);
				}
				else
				{
					reply.Failure = answer.GetError();
				}
			}
			catch (const std::exception& thrown)
			{
				reply.Failure = Core::Error{Core::ErrorCode::IoFailure, std::format("\"{}\" failed: {}", job.Request.Operation, thrown.what())};
			}

			// A frame past the limits breaks the stream for every request on it (ADR 0695), so an
			// answer too large to send is refused here, as itself, never truncated.
			const bool tooLarge = reply.Payload.size() > Core::kMaxFramePayloadBytes ||
								  reply.Result.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace).size() > Core::kMaxFrameHeaderBytes / 2;

			if (tooLarge)
			{
				reply.Failure = Core::Error{Core::ErrorCode::TooLarge,
											std::format("the answer to \"{}\" is {} bytes, more than the agent can send in one go", job.Request.Operation,
														reply.Payload.size())};
				reply.Payload.clear();
				reply.Result = nlohmann::json::object();
			}

			{
				std::scoped_lock lock{m_Mutex};
				m_Finished.push_back(std::move(reply));
			}

			const std::uint64_t one = 1;
			static_cast<void>(::write(m_Signal, &one, sizeof one));
		}
	}
}
