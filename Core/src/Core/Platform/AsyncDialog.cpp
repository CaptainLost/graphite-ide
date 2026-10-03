#include "corepch.h"

#include "Core/Platform/AsyncDialog.h"

#include "Core/App/Redraw.h"

#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace Core {

	struct AsyncDialog::Pending
	{
		std::mutex Mutex;
		std::optional<Result<std::filesystem::path>> Answer;

		// Set by the owner's destructor. Nobody will poll for the answer any more, and the
		// application may be past main, destroying Redraw's statics.
		bool Abandoned = false;
	};

	// Detached from the start rather than joined here: joining would block the thread that
	// draws on a dialog the user has not answered, which is the stall this class exists to
	// avoid. The thread owns what it touches through the shared Pending, and touches nothing
	// else once this has run: it asks for a redraw only under Pending's mutex and only while
	// the dialog is not abandoned, so after this returns it never reaches Redraw.
	AsyncDialog::~AsyncDialog()
	{
		if (m_Pending)
		{
			const std::lock_guard lock(m_Pending->Mutex);
			m_Pending->Abandoned = true;
		}
	}

	bool AsyncDialog::Start(Picker picker, Done done)
	{
		if (IsOpen())
		{
			return false;
		}

		auto pending = std::make_shared<Pending>();

		std::thread([pending, picker = std::move(picker)] {
			Result<std::filesystem::path> answer = picker();

			const std::lock_guard lock(pending->Mutex);

			if (pending->Abandoned)
			{
				return;
			}

			pending->Answer.emplace(std::move(answer));
			Redraw::Request();
		}).detach();

		m_Pending = std::move(pending);
		m_Done = std::move(done);

		return true;
	}

	bool AsyncDialog::IsOpen() const
	{
		return m_Pending != nullptr;
	}

	void AsyncDialog::Poll()
	{
		if (!m_Pending)
		{
			return;
		}

		std::optional<Result<std::filesystem::path>> answer;
		{
			const std::lock_guard lock(m_Pending->Mutex);
			answer.swap(m_Pending->Answer);
		}

		if (!answer)
		{
			return;
		}

		// Cleared before the callback, so that it sees no dialog open and can start the next.
		m_Pending.reset();
		Done done = std::exchange(m_Done, nullptr);

		done(std::move(*answer));
	}
}
