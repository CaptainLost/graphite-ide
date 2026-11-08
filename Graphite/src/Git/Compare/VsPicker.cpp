#include "Git/Compare/VsPicker.h"

#include "Git/Branches/BranchAge.h"
#include "Git/Branches/BranchListFilter.h"
#include "Git/Compare/VsLabel.h"
#include "Git/Sidebar/SidebarText.h"

#include "Core/Base/Count.h"

#include <algorithm>
#include <utility>

namespace Graphite {

	namespace {

		constexpr const char* kPending = "\xE2\x80\xA6";
		constexpr const char* kDot = " \xC2\xB7 ";

		std::string ForkedText(const std::optional<Core::LineDistance>& distance, std::chrono::sys_seconds now)
		{
			if (!distance.has_value())
			{
				return kPending;
			}

			if (!distance->ForkTime.has_value())
			{
				return "no commit in common";
			}

			const std::string age = FormatBranchAge(std::chrono::sys_seconds{std::chrono::seconds{*distance->ForkTime}}, now);
			const std::string forked = age == "now" ? "forked now" : "forked " + age + " ago";

			return forked + kDot + (distance->ForkOnLine ? "on its line" : "via a merge");
		}

		std::string CountsText(const std::optional<Core::LineDistance>& distance)
		{
			if (!distance.has_value())
			{
				return kPending;
			}

			return Core::FormatCount(distance->Ahead) + " ahead" + kDot + Core::FormatCount(distance->Behind) + " behind";
		}

		bool IsGuessedMainLine(const LineRead& read, std::size_t ref)
		{
			const std::vector<Core::LineBase>& bases = read.Model.Bases;

			return !bases.empty() && bases.front().Ref == ref && bases.front().Reason == Core::BaseReason::MainLineGuess;
		}

		// What the branch is compared with by the rule and now; for a base or an old copy, nothing by the rule and the
		// user's pick now.
		struct Comparison
		{
			std::optional<std::size_t> Automatic;
			std::optional<std::size_t> Chosen;
		};

		// Adds the options that name a ref, each ref once.
		class RefOptions
		{
		public:
			RefOptions(const VsPickerInput& input, const Comparison& comparison, std::vector<VsOption>& options)
				: m_Input(input), m_Comparison(comparison), m_Options(options)
			{
			}

			bool Includes(std::size_t ref) const { return std::find(m_Listed.begin(), m_Listed.end(), ref) != m_Listed.end(); }

			void Add(VsOption::Kind kind, std::size_t ref, std::string why = {})
			{
				const std::optional<Core::LineDistance> distance = m_Input.DistanceTo(ref);

				VsOption option;
				option.What = kind;
				option.Ref = ref;
				option.FullName = m_Input.Read.Refs[ref].FullName;
				option.Name = LineNameOf(m_Input.Read, ref);
				option.Why = why.empty() ? ForkedText(distance, m_Input.Now) : std::move(why);
				option.Counts = CountsText(distance);
				option.Automatic = m_Comparison.Automatic == ref;
				option.Chosen = m_Comparison.Chosen == ref;
				option.Guessed = kind == VsOption::Kind::Base && IsGuessedMainLine(m_Input.Read, ref);

				m_Listed.push_back(ref);
				m_Options.push_back(std::move(option));
			}

		private:
			const VsPickerInput& m_Input;
			const Comparison& m_Comparison;
			std::vector<VsOption>& m_Options;
			std::vector<std::size_t> m_Listed;
		};

		std::string OldCopyReason(const Core::OldCopy& copy, const VsPickerInput& input)
		{
			const LineRead& read = input.Read;
			const std::optional<Core::LineDistance> distance = input.OldCopyDistance(copy);
			const std::string behind = distance.has_value() ? Core::FormatCount(distance->Behind) : std::string{kPending};

			return "default branch on " + read.Refs[copy.Ref].Remote + ", but an old copy of " + LineNameOf(read, copy.Of) + " (" + behind +
				   " behind it), so it is never a line of its own";
		}

		// A base and an old copy have no line by the rule: the rule compares them with nothing, and only the user's pick
		// compares them with something.
		bool StandsAlone(const ShownComparison& shown)
		{
			return shown.What == ShownComparison::Kind::Pick || shown.What == ShownComparison::Kind::Base ||
				   shown.What == ShownComparison::Kind::OldCopy;
		}
	}

	VsPicker BuildVsPicker(const VsPickerInput& input)
	{
		VsPicker picker;

		// A base's local copy with no commits of its own is offered what its base is.
		const ShownComparison shown = input.Lines.ComparisonOf(input.Branch);
		const std::optional<std::size_t> named = input.Lines.IndexOf(input.Branch);
		const std::optional<std::size_t> self = input.Lines.IndexOf(shown.Shown);
		const bool standsAlone = StandsAlone(shown);
		const Core::BranchLine* const line = shown.What == ShownComparison::Kind::Line ? shown.Line : nullptr;

		if (!named.has_value() || !self.has_value() || (!standsAlone && line == nullptr))
		{
			return picker;
		}

		const Comparison comparison = standsAlone ? Comparison{.Automatic = std::nullopt, .Chosen = shown.Picked}
												  : Comparison{.Automatic = line->AutomaticComparedWith, .Chosen = line->ComparedWith};

		const LineRead& read = input.Read;

		picker.Subject = LineNameOf(read, *named);
		picker.Title = "Compare " + picker.Subject + " with";
		picker.Footer =
			"A branch is compared with the line it forked from: the branch it is built on, a release line, or a base. Where lines part, "
			"the line goes on along the side more people worked on. Any branch's menu has Use as base and Stop using as base.";

		RefOptions refs(input, comparison, picker.Options);

		if (line != nullptr && line->AutomaticKind == Core::ComparedKind::Branch && line->AutomaticComparedWith.has_value())
		{
			refs.Add(VsOption::Kind::BuiltOn, *line->AutomaticComparedWith, "the branch it is built on");
		}

		// A quiet base is offered only when the rule or the user compares the branch with it.
		for (const Core::LineBase& base : read.Model.Bases)
		{
			const bool comparedWithIt = comparison.Automatic == base.Ref || comparison.Chosen == base.Ref;

			if (base.Ref != *self && !refs.Includes(base.Ref) && (base.Active || comparedWithIt))
			{
				refs.Add(VsOption::Kind::Base, base.Ref);
			}
		}

		// A pick of a branch that is neither of those stays on the list, so the radio dot has a place.
		if (comparison.Chosen.has_value() && !refs.Includes(*comparison.Chosen))
		{
			refs.Add(VsOption::Kind::Branch, *comparison.Chosen);
		}

		VsOption another;
		another.What = VsOption::Kind::AnotherBranch;
		another.Name = "Another branch\xE2\x80\xA6";
		another.Why = "search all " + Core::FormatCount(read.Refs.size());
		picker.Options.push_back(std::move(another));

		const std::vector<std::string> words = LoweredWordsOf(input.Search);

		if (!words.empty() && read.Branches)
		{
			const std::vector<BranchAuthor> noAuthors;
			std::size_t found = 0;

			for (std::size_t ref = 0; ref < read.Refs.size() && ref < read.Branches->size() && found < kMostOtherBranches; ++ref)
			{
				if (ref != *self && !refs.Includes(ref) && MatchesWords((*read.Branches)[ref], noAuthors, words, false))
				{
					refs.Add(VsOption::Kind::Branch, ref);
					++found;
				}
			}
		}

		VsOption nothing;
		nothing.What = VsOption::Kind::Nothing;
		nothing.Name = "Nothing";
		nothing.Why = "the branch on its own";
		nothing.Automatic = !comparison.Automatic.has_value();
		nothing.Chosen = !comparison.Chosen.has_value();
		picker.Options.push_back(std::move(nothing));

		for (const Core::LineBase& base : read.Model.Bases)
		{
			if (!base.Active)
			{
				++picker.QuietBases;
				continue;
			}

			picker.Bases.push_back(VsBaseLine{.Name = LineNameOf(read, base.Ref), .Reason = BaseReasonText(base, read), .OldCopy = false});
		}

		for (const Core::OldCopy& copy : read.Model.OldCopies)
		{
			picker.Bases.push_back(VsBaseLine{.Name = LineNameOf(read, copy.Ref), .Reason = OldCopyReason(copy, input), .OldCopy = true});
		}

		return picker;
	}

	std::optional<LineAnswerChange> ChangeFor(const VsPicker&, const VsOption& chosen, std::string_view branch)
	{
		if (chosen.What == VsOption::Kind::AnotherBranch)
		{
			return std::nullopt;
		}

		LineAnswerChange change;
		change.Ref = std::string{branch};

		if (chosen.Automatic)
		{
			change.What = LineAnswerChange::Kind::CompareAutomatically;
		}
		else if (chosen.What == VsOption::Kind::Nothing)
		{
			change.What = LineAnswerChange::Kind::CompareWithNothing;
		}
		else
		{
			change.What = LineAnswerChange::Kind::CompareWith;
			change.Target = chosen.FullName;
		}

		return change;
	}
}
