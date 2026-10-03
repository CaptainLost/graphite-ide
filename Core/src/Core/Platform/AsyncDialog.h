#pragma once

#include "Core/Base/Base.h"

#include "Core/Base/NonCopyable.h"
#include "Core/Base/Result.h"

#include <filesystem>
#include <functional>
#include <memory>

namespace Core {

	// Runs a blocking system dialog - one of FileDialog's pickers - on a thread of its own, and
	// hands its answer back on the thread that polls.
	//
	// The thread that draws must not be the one blocked in the dialog. Closing a shell dialog
	// can wait seconds on its own change-notification thread after the dialog has already
	// gone from the screen, retrieving no input meanwhile, and Windows then marks every window
	// on that thread Not Responding and covers it until the wait ends.
	//
	// One dialog at a time: a second Start while one is open is refused. Nothing ties the
	// dialog to a window of the application, since an owner on another thread would share its
	// input queue with the dialog's and bring the stall back to that window.
	class AsyncDialog final : NonCopyable
	{
	public:
		using Picker = std::function<Result<std::filesystem::path>()>;
		using Done = std::function<void(Result<std::filesystem::path>)>;

		AsyncDialog() = default;

		// Does not wait for a dialog still open: it is left to finish on its own, and its
		// answer goes nowhere.
		~AsyncDialog();

		// Runs `picker` on a new thread. `done` gets its result on the first Poll after it
		// returns. False, and nothing started, while a dialog is already open.
		bool Start(Picker picker, Done done);

		// From Start until the result has been handed to `done`.
		bool IsOpen() const;

		// Call once a frame. Hands over the result of a dialog that has returned; `done` runs
		// here, on this thread, and may Start the next dialog.
		void Poll();

	private:
		// Shared with the dialog's thread, which outlives this object when it is destroyed
		// with a dialog open.
		struct Pending;

		Ref<Pending> m_Pending;
		Done m_Done;
	};
}
