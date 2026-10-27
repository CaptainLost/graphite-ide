#pragma once

namespace Core {

	class AsyncDialog;

}

namespace Graphite {

	inline constexpr const char* kFileDialogOpenNote = "A file dialog is open \xE2\x80\x94 click to bring it to the front";

	// WARNING: Call right after an item drawn disabled while the dialog is open. Answers whether a click brought the
	// dialog to the front, so a menu can close behind it.
	bool OfferOpenFileDialog(const Core::AsyncDialog& dialog);
}
