// The Git panel's modal questions: checking out over changes, naming a branch, deleting a
// branch and dropping a stash. Members of GitView, kept apart from GitView.cpp so the panel's
// drawing of refs and history does not have to be read past four dialogs. Their shape - the
// padding, the list, the button row and its keys - is DialogLayout's, shared with the
// Changes panel's stash dialog.

#include "Git/GitView.h"

#include "Core/Base/Base.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/ImGui/Icons.h"
#include "Theme/Palettes.h"
#include "UI/DialogLayout.h"

#include <imgui.h>

#include <cstddef>
#include <format>
#include <string>
#include <vector>

namespace Graphite {

	namespace {

		// The rows of one group in the checkout dialog, under a header naming the group.
		void CheckoutRows(const char* heading, const std::vector<CheckoutQuestionRow>& rows)
		{
			if (rows.empty())
			{
				return;
			}

			DialogLayout::FileGroupHeader(heading);

			for (const CheckoutQuestionRow& row : rows)
			{
				DialogLayout::FileRow(Core::IconForExtension(Core::ToUtf8(row.Path.extension())),
									  Core::ToGenericUtf8(row.Path), CheckoutRowTag(row));
			}
		}
	}

	ImVec4 GitView::DangerColour() const
	{
		return ImGui::ColorConvertU32ToFloat4(ReadEditorPalette(m_Scheme.Get()).DiagnosticError);
	}

	void GitView::RenderCheckoutDialog()
	{
		DialogLayout::BeforeBegin(DialogLayout::kWideWidth);

		const bool open = m_CheckoutQuestion.Begin([](const CheckoutQuestionPayload&) { return true; },
												   [](const CheckoutQuestionPayload& payload) {
													   return "Checkout " + CheckoutTargetLabel(payload.Target);
												   });

		DialogLayout::AfterBegin();

		if (!open)
		{
			m_CheckoutDialogOpen = false;
			return;
		}

		// Null for the one frame after a question was withdrawn while on screen: the popup is
		// still open, with nothing left to ask.
		const CheckoutQuestionPayload* current = m_CheckoutQuestion.GetCurrent();

		if (current == nullptr)
		{
			ImGui::CloseCurrentPopup();
			m_CheckoutQuestion.End();
			m_CheckoutDialogOpen = false;
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

		// Every change, not only the blocking ones, since both answers act on every change -
		// so the list can be long, and scrolls rather than pushing the buttons off screen.
		const std::size_t rows = question.Changed.size() + question.Untracked.size();
		const std::size_t headers = (question.Changed.empty() ? 0 : 1) + (question.Untracked.empty() ? 0 : 1);

		if (DialogLayout::BeginFileList("checkout.files", rows, headers))
		{
			CheckoutRows("CHANGED", question.Changed);
			CheckoutRows("UNTRACKED", question.Untracked);

			// BuildCheckoutQuestion is only asked when there is something to list. Should both
			// lists ever be empty, the child still ends on an item, as every window here does.
			if (rows == 0)
			{
				ImGui::Dummy(ImVec2(0.0f, 0.0f));
			}
		}

		DialogLayout::EndFileList();
		DialogLayout::Gap();

		// Locked on when an untracked file blocks: leaving it out would leave the checkout
		// refused over the very file it was asked about.
		if (question.UntrackedForced)
		{
			bool forced = true;
			ImGui::BeginDisabled();
			ImGui::Checkbox("Include untracked files in Discard and Stash", &forced);
			ImGui::EndDisabled();
		}
		else
		{
			ImGui::Checkbox("Include untracked files in Discard and Stash", &m_IncludeUntracked);
		}

		DialogLayout::Gap();

		if (m_StashSaveFailed)
		{
			// Which file failed is already in the status bar - see EditorBuffers::Save.
			DialogLayout::ErrorText("Could not save every file; nothing was stashed.", DangerColour());
		}

		DialogLayout::MutedText("Discard cannot be undone.");
		DialogLayout::Gap();

		// Copied out before Answer(), which drops the payload `current` points at.
		const Core::CheckoutTarget target = current->Target;
		const std::vector<std::filesystem::path> unsaved = current->UnsavedAbsolute;
		const bool includeUntracked = question.UntrackedForced || m_IncludeUntracked;

		const auto answer = [this] {
			m_CheckoutQuestion.Answer();

			// Let go here too, not only when Begin fails: a question queued behind this one
			// opens on the very next Begin, with no frame between them to reset on.
			m_CheckoutDialogOpen = false;
		};

		const bool writable = !m_Model.IsWriteInFlight();

		// Stash is the safe answer, so it is the one Enter gives; Discard is the one button
		// here that loses work for good, and only a click reaches it.
		const DialogLayout::Choice choice =
			DialogLayout::ButtonRow({.Label = "Stash", .Enabled = writable}, {.Label = "Discard", .Enabled = writable},
									DangerColour());

		if (choice == DialogLayout::Choice::Destructive)
		{
			// The editor's copies first: the checkout is about to write over the files, and a
			// buffer still holding the old edits would then disagree with the disk.
			m_Buffers.Revert(unsaved);
			m_Model.RequestCheckout(CheckoutRequest{.Target = target,
													.Preparation = CheckoutPreparation::Discard,
													.IncludeUntracked = includeUntracked});
			answer();
		}
		else if (choice == DialogLayout::Choice::Safe)
		{
			// A stash takes what is on disk, so the unsaved edits have to get there first - and
			// if any cannot, stashing the rest would leave those edits behind on this branch.
			if (!m_Buffers.Save(unsaved))
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
				answer();
			}
		}
		else if (choice == DialogLayout::Choice::Cancel)
		{
			answer();
		}

		m_CheckoutQuestion.End();
	}

	void GitView::RenderBranchNameDialog(ModalQuestion<BranchNamePayload>& question)
	{
		DialogLayout::BeforeBegin(DialogLayout::kNarrowWidth);

		const bool open = question.Begin([](const BranchNamePayload&) { return true; },
										 [](const BranchNamePayload& payload) {
											 return payload.Mode == BranchNameMode::New
														? "New branch from " + payload.From
														: "Rename " + payload.From;
										 });

		DialogLayout::AfterBegin();

		if (!open)
		{
			if (m_BranchNameFor == &question)
			{
				m_BranchNameFor = nullptr;
			}

			return;
		}

		const BranchNamePayload* current = question.GetCurrent();

		if (current == nullptr)
		{
			ImGui::CloseCurrentPopup();
			question.End();
			m_BranchNameFor = nullptr;
			return;
		}

		const bool first = m_BranchNameFor != &question;

		if (first)
		{
			m_BranchNameFor = &question;
			m_BranchNameBuffer = current->Buffer;
			m_CheckoutAfter = current->CheckoutAfter;
		}

		// Copied out before Answer(), which drops the payload `current` points at.
		const BranchNameMode mode = current->Mode;
		const std::string from = current->From;
		const Core::Oid at = current->At;

		// The field is what the dialog is for, so it takes the keyboard as it opens.
		if (first)
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

		// An empty field says so through its hint and the disabled button; anything typed that
		// will not do is said under it, and only then does the dialog grow a line.
		if (problem.has_value() && !name.empty())
		{
			DialogLayout::ErrorText(*problem, DangerColour());
		}

		if (mode == BranchNameMode::New)
		{
			DialogLayout::Gap();
			ImGui::Checkbox("Checkout after creating", &m_CheckoutAfter);
		}

		DialogLayout::Gap();

		const bool acceptable = !problem.has_value() && !m_Model.IsWriteInFlight();

		const DialogLayout::Choice choice = DialogLayout::ButtonRow(
			{.Label = mode == BranchNameMode::New ? "Create" : "Rename", .Enabled = acceptable}, {}, DangerColour());

		if (acceptable && (choice == DialogLayout::Choice::Safe || entered))
		{
			if (mode == BranchNameMode::New)
			{
				m_Model.RequestCreateBranch(name, at);

				// The same way as any other checkout - planned, and asked about when it would
				// overwrite something. The plan queues behind the create, so it finds the branch.
				if (m_CheckoutAfter)
				{
					BeginCheckout(Core::CheckoutTarget{.Kind = Core::CheckoutKind::LocalBranch, .Name = name});
				}
			}
			else
			{
				m_Model.RequestRenameBranch(from, name);

				// Followed once the branches list shows the new name - see FollowSelectedRef.
				if (m_SelectedRef == "refs/heads/" + from)
				{
					m_PendingRename.emplace("refs/heads/" + from, "refs/heads/" + name);
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

		const bool open = m_DeleteBranchQuestion.Begin([](const DeleteBranchPayload&) { return true; },
													   [](const DeleteBranchPayload& payload) {
														   return "Delete branch " + payload.Branch;
													   });

		DialogLayout::AfterBegin();

		if (!open)
		{
			return;
		}

		const DeleteBranchPayload* current = m_DeleteBranchQuestion.GetCurrent();

		if (current == nullptr)
		{
			ImGui::CloseCurrentPopup();
			m_DeleteBranchQuestion.End();
			return;
		}

		const std::string branch = current->Branch;
		const std::size_t unmerged = current->Unmerged;

		if (unmerged == 0)
		{
			DialogLayout::Text("Delete branch " + branch + "? It has no commits that other branches lack.");
		}
		else
		{
			DialogLayout::Text(std::format(
				"Delete branch {}? {} commit(s) on it are on no other branch and will be lost.", branch, unmerged));
		}

		DialogLayout::Gap();
		DialogLayout::MutedText("Deleting a branch cannot be undone here.");
		DialogLayout::Gap();

		// Worded for what it costs: past this button, those commits have no name left. No
		// safe action, so Enter does nothing; Escape cancels.
		const DialogLayout::Choice choice = DialogLayout::ButtonRow(
			{}, {.Label = unmerged == 0 ? "Delete" : "Delete anyway", .Enabled = !m_Model.IsWriteInFlight()},
			DangerColour());

		if (choice == DialogLayout::Choice::Destructive)
		{
			m_Model.RequestDeleteBranch(branch);
			m_DeleteBranchQuestion.Answer();
		}
		else if (choice == DialogLayout::Choice::Cancel)
		{
			m_DeleteBranchQuestion.Answer();
		}

		m_DeleteBranchQuestion.End();
	}

	void GitView::RenderDropStashDialog()
	{
		DialogLayout::BeforeBegin(DialogLayout::kNarrowWidth);

		const bool open = m_DropStashQuestion.Begin([](const DropStashPayload&) { return true; },
													[](const DropStashPayload& payload) {
														return std::format("Drop stash@{{{}}}", payload.Index);
													});

		DialogLayout::AfterBegin();

		if (!open)
		{
			return;
		}

		const DropStashPayload* current = m_DropStashQuestion.GetCurrent();

		if (current == nullptr)
		{
			ImGui::CloseCurrentPopup();
			m_DropStashQuestion.End();
			return;
		}

		const Core::Oid id = current->Id;

		DialogLayout::Text(std::format("Drop stash@{{{}}}? The changes it holds are deleted with it.", current->Index));
		DialogLayout::Gap();
		DialogLayout::MutedText("A dropped stash cannot be recovered.");
		DialogLayout::Gap();

		const DialogLayout::Choice choice = DialogLayout::ButtonRow(
			{}, {.Label = "Drop", .Enabled = !m_Model.IsWriteInFlight()}, DangerColour());

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
