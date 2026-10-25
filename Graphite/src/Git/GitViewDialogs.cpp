#include "Git/GitView.h"

#include "Core/Base/Base.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/ImGui/LoadingState.h"
#include "Git/StashBranch.h"
#include "Theme/Palettes.h"
#include "UI/Dialogs/DialogLayout.h"
#include "UI/FileIcons.h"
#include "UI/TickBox.h"

#include <imgui.h>

#include <cstddef>
#include <format>
#include <string>
#include <vector>

namespace Graphite {

	namespace {

		void CheckoutRows(const char* heading, const std::vector<CheckoutQuestionRow>& rows)
		{
			if (rows.empty())
			{
				return;
			}

			DialogLayout::FileGroupHeader(heading);

			for (const CheckoutQuestionRow& row : rows)
			{
				DialogLayout::FileRow(KindOfFile(row.Path), Core::ToGenericUtf8(row.Path), CheckoutRowTag(row));
			}
		}
	}

	void GitView::RenderDetachDialog()
	{
		DialogLayout::BeforeBegin(DialogLayout::kWideWidth);

		const DetachPayload* current = m_DetachQuestion.Begin(
			[](const DetachPayload&) { return true; },
			[](const DetachPayload& payload) { return "Checkout " + payload.Commit.ShortHex() + " (detached)"; });

		DialogLayout::AfterBegin();

		if (current == nullptr)
		{
			return;
		}

		// WARNING: Every dialog copies what it needs out of the payload before calling Answer(),
		// which drops the payload `current` points at.
		const Core::Oid commit = current->Commit;

		DialogLayout::Text(DetachedCheckoutBody(commit.ShortHex()));
		DialogLayout::Gap();

		const bool writable = !m_Model.IsWriteUnsettled();
		const DialogLayout::ButtonSpec absentButton{};

		const DialogLayout::Choice choice = DialogLayout::ButtonRow(
			{.Label = "Checkout", .Enabled = writable}, absentButton, DangerColourOf(m_Scheme.Get()),
			{.Label = "Create Branch Instead…", .Enabled = writable});

		if (choice == DialogLayout::Choice::Safe)
		{
			BeginCheckout(Core::CheckoutTarget{.Kind = Core::CheckoutKind::Commit, .Commit = commit});
			m_DetachQuestion.Answer();
		}
		else if (choice == DialogLayout::Choice::Alternative)
		{
			BeginNewBranch(commit.ShortHex(), commit);
			m_DetachQuestion.Answer();
		}
		else if (choice == DialogLayout::Choice::Cancel)
		{
			m_DetachQuestion.Answer();
		}

		m_DetachQuestion.End();
	}

	void GitView::RenderCheckoutDialog()
	{
		DialogLayout::BeforeBegin(DialogLayout::kWideWidth);

		const CheckoutQuestionPayload* current = m_CheckoutQuestion.Begin(
			[](const CheckoutQuestionPayload&) { return true; },
			[](const CheckoutQuestionPayload& payload) { return "Checkout " + CheckoutTargetLabel(payload.Target); });

		DialogLayout::AfterBegin();

		if (current == nullptr)
		{
			m_CheckoutDialogOpen = false;

			// WARNING: Escape closes the popup and drops the question without reaching the code below.
			if (!m_CheckoutQuestion.IsAsked())
			{
				m_AskedCheckoutPlan.reset();
			}

			return;
		}

		if (!m_CheckoutDialogOpen)
		{
			m_CheckoutDialogOpen = true;
			m_IncludeUntracked = current->IncludeUntracked;
			m_StashSaveFailed = false;
		}

		const CheckoutQuestion& question = current->Question;

		DialogLayout::Text(question.Title + ".");
		DialogLayout::Gap();

		const std::size_t rowCount = question.Changed.size() + question.Untracked.size();
		const std::size_t headerCount = (question.Changed.empty() ? 0 : 1) + (question.Untracked.empty() ? 0 : 1);

		if (DialogLayout::BeginFileList("checkout.files", rowCount, headerCount))
		{
			CheckoutRows("CHANGED", question.Changed);
			CheckoutRows("UNTRACKED", question.Untracked);

			// WARNING: Should both lists ever be empty, the child must still end on an item.
			if (rowCount == 0)
			{
				ImGui::Dummy(ImVec2(0.0f, 0.0f));
			}
		}

		DialogLayout::EndFileList();
		DialogLayout::Gap();

		if (question.UntrackedForced)
		{
			bool alwaysIncluded = true;
			ImGui::BeginDisabled();
			DrawTickBox("Include untracked files in Discard and Stash", &alwaysIncluded);
			ImGui::EndDisabled();
		}
		else
		{
			DrawTickBox("Include untracked files in Discard and Stash", &m_IncludeUntracked);
		}

		DialogLayout::Gap();

		if (m_StashSaveFailed)
		{
			DialogLayout::ErrorText("Could not save every file; nothing was stashed.", DangerColourOf(m_Scheme.Get()));
		}

		DialogLayout::MutedText("Discard cannot be undone.");
		DialogLayout::Gap();

		const Core::CheckoutTarget target = current->Target;
		const std::vector<std::filesystem::path> unsaved = current->UnsavedAbsolute;
		const bool includeUntracked = question.UntrackedForced || m_IncludeUntracked;

		const bool writable = !m_Model.IsWriteUnsettled();

		// WARNING: Stash is the safe answer, so it is the one Enter gives. Discard loses work for
		// good, and only a click reaches it.
		const DialogLayout::Choice choice =
			DialogLayout::ButtonRow({.Label = "Stash", .Enabled = writable}, {.Label = "Discard", .Enabled = writable},
									DangerColourOf(m_Scheme.Get()));

		if (choice == DialogLayout::Choice::Destructive)
		{
			// WARNING: The editor's copies are reverted first. The checkout writes over the files, and
			// a buffer still holding the old edits would disagree with the disk.
			m_Buffers.Revert(unsaved);
			m_Model.RequestCheckout(CheckoutRequest{.Target = target,
													.Preparation = CheckoutPreparation::Discard,
													.IncludeUntracked = includeUntracked});
			AnswerCheckoutQuestion();
		}
		else if (choice == DialogLayout::Choice::Safe)
		{
			// WARNING: A stash takes what is on disk, so unsaved edits are saved first. If any cannot
			// be, stashing the rest would leave those edits behind on this branch.
			const bool everyFileSaved = m_Buffers.Save(unsaved);

			if (!everyFileSaved)
			{
				m_StashSaveFailed = true;
			}
			else
			{
				m_Model.RequestCheckout(CheckoutRequest{.Target = target,
														.Preparation = CheckoutPreparation::Stash,
														.IncludeUntracked = includeUntracked,
														.StashMessage = "Before checking out " +
																		CheckoutTargetLabel(target)});
				AnswerCheckoutQuestion();
			}
		}
		else if (choice == DialogLayout::Choice::Cancel)
		{
			AnswerCheckoutQuestion();
		}

		m_CheckoutQuestion.End();
	}

	void GitView::AnswerCheckoutQuestion()
	{
		m_CheckoutQuestion.Answer();
		m_AskedCheckoutPlan.reset();

		// WARNING: Reset here as well as when Begin fails: a question queued behind this one opens on
		// the very next Begin, with no frame between them to reset on.
		m_CheckoutDialogOpen = false;
	}

	void GitView::RenderBranchNameDialog(ModalQuestion<BranchNamePayload>& question)
	{
		DialogLayout::BeforeBegin(DialogLayout::kNarrowWidth);

		const BranchNamePayload* current = question.Begin([](const BranchNamePayload&) { return true; },
														  [](const BranchNamePayload& payload) {
															  return payload.Mode == BranchNameMode::New
																		 ? "New branch from " + payload.From
																		 : "Rename " + payload.From;
														  });

		DialogLayout::AfterBegin();

		if (current == nullptr)
		{
			if (m_BranchNameFor == &question)
			{
				m_BranchNameFor = nullptr;
			}

			return;
		}

		const bool isFirstFrameOfQuestion = m_BranchNameFor != &question;

		if (isFirstFrameOfQuestion)
		{
			m_BranchNameFor = &question;
			m_BranchNameBuffer = current->Buffer;
			m_CheckoutAfter = current->CheckoutAfter;
		}

		const BranchNameMode mode = current->Mode;
		const std::string from = current->From;
		const Core::Oid at = current->At;

		if (isFirstFrameOfQuestion)
		{
			ImGui::SetKeyboardFocusHere();
		}

		ImGui::SetNextItemWidth(-FLT_MIN);
		const bool entered =
			ImGui::InputTextWithHint("##branchName", "Branch name", m_BranchNameBuffer.data(),
									 m_BranchNameBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue);

		const std::string name(m_BranchNameBuffer.data());

		const Core::Ref<const std::vector<Core::Branch>> branches = m_Model.GetBranches();
		const std::optional<std::string> problem =
			BranchNameProblem(name, branches ? *branches : std::vector<Core::Branch>{});

		if (problem.has_value() && !name.empty())
		{
			DialogLayout::ErrorText(*problem, DangerColourOf(m_Scheme.Get()));
		}

		if (mode == BranchNameMode::New)
		{
			DialogLayout::Gap();
			DrawTickBox("Checkout after creating", &m_CheckoutAfter);
		}

		DialogLayout::Gap();

		const bool acceptable = !problem.has_value() && !m_Model.IsWriteUnsettled();
		const DialogLayout::ButtonSpec absentButton{};

		const DialogLayout::Choice choice = DialogLayout::ButtonRow(
			{.Label = mode == BranchNameMode::New ? "Create" : "Rename", .Enabled = acceptable}, absentButton,
			DangerColourOf(m_Scheme.Get()));

		if (acceptable && (choice == DialogLayout::Choice::Safe || entered))
		{
			if (mode == BranchNameMode::New)
			{
				m_Model.RequestCreateBranch(name, at);

				// WARNING: The checkout plan queues behind the create, so it finds the new branch.
				if (m_CheckoutAfter)
				{
					BeginCheckout(Core::CheckoutTarget{.Kind = Core::CheckoutKind::LocalBranch, .Name = name});
				}
			}
			else
			{
				const std::string fromFullName = Core::LocalBranchReference(from);
				const std::string toFullName = Core::LocalBranchReference(name);

				m_Model.RequestRenameBranch(from, name);

				if (OnBranchRenamed)
				{
					OnBranchRenamed(fromFullName, toFullName);
				}

				if (m_SelectedRef == fromFullName)
				{
					m_PendingRename = BranchRenameSent{.From = fromFullName, .To = toFullName};
				}
			}

			question.Answer();
			m_BranchNameFor = nullptr;
		}
		else if (choice == DialogLayout::Choice::Cancel)
		{
			question.Answer();
			m_BranchNameFor = nullptr;
		}

		question.End();
	}

	void GitView::RenderDeleteBranchDialog()
	{
		DialogLayout::BeforeBegin(DialogLayout::kNarrowWidth);

		const DeleteBranchPayload* current =
			m_DeleteBranchQuestion.Begin([](const DeleteBranchPayload&) { return true; },
										 [](const DeleteBranchPayload& payload) { return "Delete branch " + payload.Branch; });

		DialogLayout::AfterBegin();

		if (current == nullptr)
		{
			// WARNING: Escape closes the popup and drops the question without reaching the code below.
			const bool dismissedByReader = !m_DeleteBranchQuestion.IsAsked() && !m_DeleteHeldForPanel;

			if (dismissedByReader)
			{
				m_DeleteUnanswered.reset();
				m_DeleteUnmerged.reset();
			}

			return;
		}

		const std::string branch = current->Branch;
		const std::optional<std::size_t> unmerged = UnmergedCountFor(branch);
		const bool noCommitsLost = unmerged == std::optional<std::size_t>{0};

		if (!unmerged.has_value())
		{
			DialogLayout::Text("Delete branch " + branch + "?");
			Core::LoadingLine("Counting commits not merged anywhere else…", Core::WhileLoaderWaits::ShowText);
		}
		else if (noCommitsLost)
		{
			DialogLayout::Text("Delete branch " + branch + "? It has no commits that other branches lack.");
		}
		else
		{
			DialogLayout::Text(std::format(
				"Delete branch {}? {} commit(s) on it are on no other branch and will be lost.", branch, *unmerged));
		}

		DialogLayout::Gap();
		DialogLayout::MutedText("Deleting a branch cannot be undone here.");
		DialogLayout::Gap();

		const DialogLayout::ButtonSpec absentButton{};
		const bool countKnown = unmerged.has_value();
		const bool loses = countKnown && !noCommitsLost;

		const DialogLayout::Choice choice = DialogLayout::ButtonRow(
			absentButton,
			{.Label = loses ? "Delete anyway" : "Delete", .Enabled = countKnown && !m_Model.IsWriteUnsettled()},
			DangerColourOf(m_Scheme.Get()));

		if (choice == DialogLayout::Choice::Destructive)
		{
			m_Model.RequestDeleteBranch(branch);
			m_DeleteBranchQuestion.Answer();
			m_DeleteUnanswered.reset();
			m_DeleteUnmerged.reset();
		}
		else if (choice == DialogLayout::Choice::Cancel)
		{
			m_DeleteBranchQuestion.Answer();
			m_DeleteUnanswered.reset();
			m_DeleteUnmerged.reset();
		}

		m_DeleteBranchQuestion.End();
	}

	void GitView::RenderDropStashDialog()
	{
		DialogLayout::BeforeBegin(DialogLayout::kNarrowWidth);

		const DropStashPayload* current = m_DropStashQuestion.Begin(
			[](const DropStashPayload&) { return true; },
			[](const DropStashPayload& payload) { return "Drop " + DropStashSubject(payload.Index, payload.Message); });

		DialogLayout::AfterBegin();

		if (current == nullptr)
		{
			return;
		}

		const Core::Oid id = current->Id;

		DialogLayout::Text("Drop " + DropStashSubject(current->Index, current->Message) +
						   "? The changes it holds are deleted with it.");
		DialogLayout::Gap();
		DialogLayout::MutedText("A dropped stash cannot be recovered.");
		DialogLayout::Gap();

		const DialogLayout::ButtonSpec absentButton{};

		const DialogLayout::Choice choice = DialogLayout::ButtonRow(
			absentButton, {.Label = "Drop", .Enabled = !m_Model.IsWriteUnsettled()}, DangerColourOf(m_Scheme.Get()));

		if (choice == DialogLayout::Choice::Destructive)
		{
			m_Model.RequestStashDrop(id);
			m_DropStashQuestion.Answer();
		}
		else if (choice == DialogLayout::Choice::Cancel)
		{
			m_DropStashQuestion.Answer();
		}

		m_DropStashQuestion.End();
	}
}
