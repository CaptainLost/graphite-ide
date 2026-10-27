#include "UI/Dialogs/FileDialogOffer.h"

#include "Core/ImGui/DisabledOffer.h"
#include "Core/Platform/AsyncDialog.h"

namespace Graphite {

	bool OfferOpenFileDialog(const Core::AsyncDialog& dialog)
	{
		if (!dialog.IsOpen() || !Core::OfferWhileDisabled(kFileDialogOpenNote))
		{
			return false;
		}

		dialog.BringToFront();
		return true;
	}
}
