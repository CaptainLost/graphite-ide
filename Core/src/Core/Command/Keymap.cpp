#include "corepch.h"

#include "Core/Command/Keymap.h"

#include "Core/Command/CommandRegistry.h"
#include "Core/Command/TypingRules.h"

#include <utility>

namespace Core {

	namespace {
		using BindingsById = std::map<std::string, Binding, std::less<>>;

		struct ChordOwner
		{
			Chord HeldChord;
			std::string CommandId;
		};

		const Chord* FindGesture(const Binding& binding, const Chord& gesture)
		{
			if (binding.Primary.IsSameGesture(gesture))
			{
				return &binding.Primary;
			}

			for (const Chord& alternate : binding.Alternates)
			{
				if (alternate.IsSameGesture(gesture))
				{
					return &alternate;
				}
			}

			return nullptr;
		}

		bool ContainsChord(const Binding& binding, const Chord& gesture)
		{
			return FindGesture(binding, gesture) != nullptr;
		}

		const Binding* FindBinding(const BindingsById& bindings, std::string_view commandId)
		{
			const BindingsById::const_iterator entry = bindings.find(commandId);

			if (entry == bindings.end())
			{
				return nullptr;
			}

			const auto& [boundCommandId, binding] = *entry;

			return &binding;
		}

		bool FiresInScope(const Binding& binding, const std::string& commandId, const Chord& gesture, ShortcutScope scope,
						  TypingTarget typingTarget, const CommandRegistry& commands)
		{
			const Chord* bound = FindGesture(binding, gesture);

			return bound != nullptr && commands.KeyScopeOf(commandId) == scope && FiresIn(*bound, typingTarget);
		}

		void ClaimChord(const Chord& chord, const std::string& commandId, std::vector<ChordOwner>& owners,
						std::vector<BindingConflict>& conflicts)
		{
			if (chord.IsEmpty())
			{
				return;
			}

			for (const ChordOwner& owner : owners)
			{
				const bool heldByAnotherCommand = owner.HeldChord.IsSameGesture(chord) && owner.CommandId != commandId;
				if (heldByAnotherCommand)
				{
					conflicts.push_back({.SharedChord = chord, .FirstCommandId = owner.CommandId, .SecondCommandId = commandId});
					return;
				}
			}

			owners.push_back({.HeldChord = chord, .CommandId = commandId});
		}

		void ClaimEveryChordOf(const std::string& commandId, const Binding& binding, std::vector<ChordOwner>& owners,
							   std::vector<BindingConflict>& conflicts)
		{
			ClaimChord(binding.Primary, commandId, owners, conflicts);

			for (const Chord& alternate : binding.Alternates)
			{
				ClaimChord(alternate, commandId, owners, conflicts);
			}
		}
	}

	void Keymap::SetDefault(std::string id, Binding binding)
	{
		m_Defaults[std::move(id)] = std::move(binding);
	}

	void Keymap::Override(std::string_view commandId, Binding binding)
	{
		m_Overrides[std::string(commandId)] = std::move(binding);
	}

	void Keymap::RestoreDefault(std::string_view commandId)
	{
		const BindingsById::const_iterator existingOverride = m_Overrides.find(commandId);
		if (existingOverride != m_Overrides.end())
		{
			m_Overrides.erase(existingOverride);
		}
	}

	void Keymap::ClearOverrides()
	{
		m_Overrides.clear();
	}

	const Binding* Keymap::For(std::string_view id) const
	{
		const Binding* userOverride = FindBinding(m_Overrides, id);

		if (userOverride != nullptr)
		{
			return userOverride;
		}

		return DefaultFor(id);
	}

	const Binding* Keymap::DefaultFor(std::string_view id) const
	{
		return FindBinding(m_Defaults, id);
	}

	bool Keymap::IsOverridden(std::string_view id) const
	{
		return FindBinding(m_Overrides, id) != nullptr;
	}

	std::string_view Keymap::BoundTo(const Chord& chord) const
	{
		std::string_view owningCommandId;

		// Through ForEachBinding rather than over the two maps directly, so this cannot start
		// disagreeing with For about which layer wins.
		ForEachBinding(
			[&owningCommandId, &chord](const std::string& id, const Binding& binding) {
				const bool ownerFound = !owningCommandId.empty();
				if (!ownerFound && ContainsChord(binding, chord))
				{
					owningCommandId = id;
				}
			});

		return owningCommandId;
	}

	std::string_view Keymap::BoundTo(const Chord& chord, ShortcutScope scope,
									 const CommandRegistry& commands) const
	{
		return BoundTo(chord, scope, TypingTarget::Nothing, commands);
	}

	std::string_view Keymap::BoundTo(const Chord& chord, ShortcutScope scope, TypingTarget typingTarget,
									 const CommandRegistry& commands) const
	{
		std::string_view owningCommandId;

		ForEachBinding(
			[&owningCommandId, &chord, scope, typingTarget, &commands](const std::string& id, const Binding& binding) {
				const bool ownerFound = !owningCommandId.empty();
				if (!ownerFound && FiresInScope(binding, id, chord, scope, typingTarget, commands))
				{
					owningCommandId = id;
				}
			});

		return owningCommandId;
	}

	std::vector<BindingConflict> Keymap::FindConflicts() const
	{
		std::vector<ChordOwner> owners;
		std::vector<BindingConflict> conflicts;

		ForEachBinding(
			[&owners, &conflicts](const std::string& id, const Binding& binding) {
				ClaimEveryChordOf(id, binding, owners, conflicts);
			});

		return conflicts;
	}

	void Keymap::ForEachOverride(
		const std::function<void(const std::string&, const Binding&)>& action) const
	{
		for (const auto& [id, binding] : m_Overrides)
		{
			action(id, binding);
		}
	}

	void Keymap::ForEachBinding(
		const std::function<void(const std::string&, const Binding&)>& action) const
	{
		for (const auto& [id, defaultBinding] : m_Defaults)
		{
			const Binding* userOverride = FindBinding(m_Overrides, id);
			const bool isOverridden = userOverride != nullptr;

			action(id, isOverridden ? *userOverride : defaultBinding);
		}

		for (const auto& [id, overrideBinding] : m_Overrides)
		{
			const bool hasNoDefault = FindBinding(m_Defaults, id) == nullptr;
			if (hasNoDefault)
			{
				action(id, overrideBinding);
			}
		}
	}
}
