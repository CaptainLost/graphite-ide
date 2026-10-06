#include "corepch.h"

#include "Core/Platform/AsyncDialog.h"

#include "Core/App/Redraw.h"
#include "Core/Base/Base.h"

#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace Core {

	struct AsyncDialog::PendingDialog
	{
		std::mutex Mutex;
		std::optional<Result<std::filesystem::path>> Answer;
		bool AbandonedByOwner = false;
	};

	// Does not join the picker's thread: joining would block the thread that draws on a dialog the
	// user has not answered. That thread asks for a redraw only under the mutex and only while the
	// dialog is not abandoned, so once this returns it never reaches Redraw, whose statics may
	// already be gone when the application is past main.
	AsyncDialog::~AsyncDialog()
	{
		if (IsOpen())
		{
			const std::lock_guard lock(m_PendingDialog->Mutex);
			m_PendingDialog->AbandonedByOwner = true;
		}
	}

	bool AsyncDialog::Start(Picker picker, AnswerHandler answerHandler)
	{
		if (IsOpen())
		{
			return false;
		}

		auto pendingDialog = CreateRef<PendingDialog>();

		std::thread([pendingDialog, picker = std::move(picker)] { PickAndHandOver(picker, *pendingDialog); }).detach();

		m_PendingDialog = std::move(pendingDialog);
		m_AnswerHandler = std::move(answerHandler);

		return true;
	}

	bool AsyncDialog::IsOpen() const
	{
		return m_PendingDialog != nullptr;
	}

	void AsyncDialog::Poll()
	{
		if (!IsOpen())
		{
			return;
		}

		std::optional<Result<std::filesystem::path>> answer;
		{
			const std::lock_guard lock(m_PendingDialog->Mutex);
			answer.swap(m_PendingDialog->Answer);
		}

		if (!answer)
		{
			return;
		}

		m_PendingDialog.reset();
		AnswerHandler answerHandler = std::exchange(m_AnswerHandler, nullptr);

		answerHandler(std::move(*answer));
	}

	void AsyncDialog::PickAndHandOver(const Picker& picker, PendingDialog& pendingDialog)
	{
		Result<std::filesystem::path> answer = picker();

		const std::lock_guard lock(pendingDialog.Mutex);

		if (pendingDialog.AbandonedByOwner)
		{
			return;
		}

		pendingDialog.Answer.emplace(std::move(answer));
		Redraw::Request();
	}
}
