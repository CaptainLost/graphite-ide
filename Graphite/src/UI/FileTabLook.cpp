#include "UI/FileTabLook.h"

#include "UI/FileIcons.h"

namespace Graphite {

	Core::DockTabLook FileTabLook(const FileTab& tab, ImFont* italicFace)
	{
		const FileKind kind = KindOfFile(tab.Path);

		Core::DockTabLook look;
		look.Name = tab.Title;
		look.Lead = Core::DockTabLead{.Width = FileIconWidth(), .Draw = [kind](ImVec2 lineTopLeft) { DrawFileIcon(kind, lineTopLeft); }};
		look.Face = tab.Preview ? italicFace : nullptr;
		look.Dot = tab.Dot;

		return look;
	}
}
