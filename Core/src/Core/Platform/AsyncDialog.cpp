#include "corepch.h"

#include "Core/Platform/AsyncDialog.h"

#include "Core/App/Redraw.h"
#include "Core/Base/Base.h"
#include "Core/Platform/ThreadWindows.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace Core {

	namespace {

		constexpr std::uint32_t kNoPickerThreadYet = 0;

#ifdef CORE_PLATFORM_WINDOWS
		std::uint32_t CallingThreadSystemId()
		{
			return static_cast<std::uint32_t>(::GetCurrentThreadId());
		}

		void BringShownWindowOfThreadToFront(std::uint32_t thread)
		{
			const HWND shown = FindShownWindowOfThread(static_cast<DWORD>(thread));

			if (shown != nullptr)
			{
				BringWindowToFront(shown);
			}
		}
#else
		std::uint32_t CallingThreadSystemId()
		{
			return kNoPickerThreadYet;
		}

		void BringShownWindowOfThreadToFront(std::uint32_t)
		{
		}
#endif
	}

	struct AsyncDialog::PendingDialog
	{
		std::mutex Mutex;
		std::optional<Result<std::filesystem::path>> Answer;
		bool AbandonedByOwner = false;
		std::uint32_t PickerThread = kNoPickerThreadYet;
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

	void AsyncDialog::BringToFront() const
	{
		if (!IsOpen())
		{
			return;
		}

		std::uint32_t pickerThread = kNoPickerThreadYet;
		{
			const std::lock_guard lock(m_PendingDialog->Mutex);
			pickerThread = m_PendingDialog->PickerThread;
		}

		if (pickerThread == kNoPickerThreadYet)
		{
			return;
		}

		BringShownWindowOfThreadToFront(pickerThread);
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
		{
			const std::lock_guard lock(pendingDialog.Mutex);
			pendingDialog.PickerThread = CallingThreadSystemId();
		}

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
