#pragma once

#include "Core/Base/Base.h"
#include "Core/Jobs/CancellationToken.h"
#include "Core/Jobs/JobSubmitter.h"
#include "Core/Jobs/Mailbox.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <utility>

namespace Core {

	// The answer to the newest of a series of questions put to a worker, when only the newest counts: a folder listed
	// again after every change, a file decoded again after it changed. Asking again cancels the work asked before, and
	// an answer to any question but the newest is dropped when it lands. The work holds only its own mailbox, so it may
	// finish after its asker is gone. WARNING: UI thread only, but for the work itself.
	template<typename T>
	class LatestAnswer
	{
	public:
		using Work = std::function<T(const CancellationToken& cancellation)>;

		explicit LatestAnswer(JobSubmitter submit) : m_Submit(std::move(submit)) {}

		~LatestAnswer() { m_Cancellation.Cancel(); }

		// WARNING: The token is shared rather than moved, and the source keeps a fresh one of its own: a moved-from token
		// holds nothing, and the source's destructor would cancel through it.
		LatestAnswer(LatestAnswer&& other)
			: m_Submit(std::move(other.m_Submit)), m_Landed(other.m_Landed), m_Cancellation(other.m_Cancellation),
			  m_Asked(other.m_Asked), m_InFlight(other.m_InFlight)
		{
			other.m_Cancellation = CancellationToken{};
			other.m_InFlight = false;
		}

		LatestAnswer(const LatestAnswer&) = delete;
		LatestAnswer& operator=(const LatestAnswer&) = delete;
		LatestAnswer& operator=(LatestAnswer&&) = delete;

		// With a submitter that runs the work where it is asked, the answer is ready to take as soon as this returns.
		void Ask(Work work)
		{
			m_Cancellation.Cancel();
			m_Cancellation = CancellationToken{};

			const std::uint64_t question = ++m_Asked;
			m_InFlight = true;

			m_Submit([landed = m_Landed, question, cancellation = m_Cancellation, work = std::move(work)] {
				if (cancellation.IsCancelled())
				{
					return;
				}

				landed->Post(Landed{question, work(cancellation)});
			});
		}

		// The newest question's answer, once, when it has landed.
		std::optional<T> TakeNewest()
		{
			std::optional<T> newest;

			for (Landed& landed : m_Landed->TakeAll())
			{
				const bool answersTheNewestQuestion = m_InFlight && landed.Question == m_Asked;

				if (answersTheNewestQuestion)
				{
					newest = std::move(landed.Answer);
				}
			}

			if (newest.has_value())
			{
				m_InFlight = false;
			}

			return newest;
		}

		// Asked and not yet taken.
		bool IsInFlight() const { return m_InFlight; }

		// Cancels the work under way; its answer is dropped when it lands.
		void Forget()
		{
			m_Cancellation.Cancel();
			++m_Asked;
			m_InFlight = false;
		}

	private:
		struct Landed
		{
			std::uint64_t Question = 0;
			T Answer;
		};

		JobSubmitter m_Submit;
		Ref<Mailbox<Landed>> m_Landed = CreateRef<Mailbox<Landed>>();
		CancellationToken m_Cancellation;

		std::uint64_t m_Asked = 0;
		bool m_InFlight = false;
	};
}
