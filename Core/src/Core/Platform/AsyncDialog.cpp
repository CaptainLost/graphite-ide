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

	bool AsyncDialog::Start(Picker picker, PickedHandler onPicked, FailureHandler onFailed)
	{
		if (IsOpen())
		{
			return false;
		}

		auto pendingDialog = CreateRef<PendingDialog>();

		std::thread([pendingDialog, picker = std::move(picker)] { PickAndHandOver(picker, *pendingDialog); }).detach();

		m_PendingDialog = std::move(pendingDialog);
		m_OnPicked = std::move(onPicked);
		m_OnFailed = std::move(onFailed);

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
		const PickedHandler onPicked = std::exchange(m_OnPicked, nullptr);
		const FailureHandler onFailed = std::exchange(m_OnFailed, nullptr);

		if (!answer->HasValue())
		{
			onFailed(answer->GetError());
			return;
		}

		const bool cancelled = answer->GetValue().empty();

		if (cancelled)
		{
			return;
		}

		onPicked(answer->GetValue());
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
