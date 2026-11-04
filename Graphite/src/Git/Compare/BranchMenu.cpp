#include "Git/Compare/BranchMenu.h"

namespace Graphite {

	namespace {

		bool HasServerDefaultBase(const LineRead& read)
		{
			for (const Core::LineBase& base : read.Model.Bases)
			{
				if (base.Reason == Core::BaseReason::ServerDefault)
				{
					return true;
				}
			}

			return false;
		}
	}

	BranchMenu MenuFor(const LineRead* read, const BranchLines* lines, std::string_view fullName)
	{
		BranchMenu menu;

		if (read == nullptr || lines == nullptr || !lines->IndexOf(fullName).has_value())
		{
			return menu;
		}

		const Core::LineBase* const base = lines->BaseNamed(fullName);
		const bool isBase = base != nullptr;

		menu.StopUsingAsBase = isBase;
		menu.UseAsBase = !isBase;
		menu.CompareWith = true;

		const bool isChosenMainLine = isBase && base->Reason == Core::BaseReason::MainLineByUser;
		menu.UseAsMainLine = !HasServerDefaultBase(*read) && !isChosenMainLine;

		return menu;
	}
}
