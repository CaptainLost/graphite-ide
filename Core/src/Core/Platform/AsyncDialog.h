#pragma once

#include "Core/Base/Base.h"

#include "Core/Base/NonCopyable.h"
#include "Core/Base/Result.h"

#include <filesystem>
#include <functional>
#include <memory>

namespace Core {

	// The thread that draws must not be the one blocked in a shell dialog. Closing one can wait
	// seconds on the dialog's own change-notification thread, and Windows marks every window on the
	// waiting thread Not Responding until the wait ends.
	class AsyncDialog final : NonCopyable
	{
	public:
		using Picker = std::function<Result<std::filesystem::path>()>;
		using PickedHandler = std::function<void(const std::filesystem::path&)>;
		using FailureHandler = std::function<void(const Error&)>;

		AsyncDialog() = default;

		~AsyncDialog();

		// WARNING: a cancelled dialog calls neither handler. The picker answers a cancel with an empty
		// path, and a handler that took it for a pick would act on nothing, such as clearing a setting.
		bool Start(Picker picker, PickedHandler onPicked, FailureHandler onFailed);

		bool IsOpen() const;

		// WARNING: The dialog has no owner window, so it can fall behind the application. This raises it again, and
		// does nothing when no dialog is open or its window is not up yet.
		void BringToFront() const;

		void Poll();

	private:
		struct PendingDialog;

		static void PickAndHandOver(const Picker& picker, PendingDialog& pendingDialog);

		Ref<PendingDialog> m_PendingDialog;
		PickedHandler m_OnPicked;
		FailureHandler m_OnFailed;
	};
}
