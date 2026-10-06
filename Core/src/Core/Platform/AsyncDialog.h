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
		using AnswerHandler = std::function<void(Result<std::filesystem::path>)>;

		AsyncDialog() = default;

		~AsyncDialog();

		bool Start(Picker picker, AnswerHandler answerHandler);

		bool IsOpen() const;

		void Poll();

	private:
		struct PendingDialog;

		static void PickAndHandOver(const Picker& picker, PendingDialog& pendingDialog);

		Ref<PendingDialog> m_PendingDialog;
		AnswerHandler m_AnswerHandler;
	};
}
