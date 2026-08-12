#pragma once

#include "Core/Base/Color.h"

namespace Core {

	class Theme
	{
	public:
		static void ApplyDark();

		static Color GetBorderColor();
	};
}