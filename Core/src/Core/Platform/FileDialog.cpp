#include "corepch.h"

#include "Core/Platform/FileDialog.h"

#include "Core/FileSystem/FileSystem.h"

#ifdef CORE_PLATFORM_WINDOWS
#include <windows.h>

#include <shobjidl.h>

#include <memory>
#include <vector>
#endif

namespace Core {

#ifdef CORE_PLATFORM_WINDOWS
	namespace {
		constexpr std::size_t kStringsPerFilter = 2;
		constexpr UINT kFirstFilterCountingFromOne = 1;

		// No owner window: AsyncDialog runs the pickers on a thread of their own, and an owner on
		// another thread would share its input queue with the dialog's and stall along with it.
		constexpr HWND kNoOwnerWindow = nullptr;

		class ComGuard
		{
		public:
			ComGuard()
			{
				const HRESULT initialiseStatus = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED |
																			   COINIT_DISABLE_OLE1DDE);
				const bool runsInTheOtherApartmentModel = initialiseStatus == RPC_E_CHANGED_MODE;

				// S_FALSE, COM already initialised on this thread, is a success too and still needs its CoUninitialize.
				m_MustUninitialise = SUCCEEDED(initialiseStatus);
				m_Usable = m_MustUninitialise || runsInTheOtherApartmentModel;
			}

			~ComGuard()
			{
				if (m_MustUninitialise)
				{
					::CoUninitialize();
				}
			}

			ComGuard(const ComGuard&) = delete;
			ComGuard& operator=(const ComGuard&) = delete;

			bool IsUsable() const { return m_Usable; }

		private:
			bool m_MustUninitialise = false;
			bool m_Usable = false;
		};

		struct ComRelease
		{
			void operator()(IUnknown* object) const { object->Release(); }
		};

		struct CoTaskMemDelete
		{
			void operator()(wchar_t* text) const { ::CoTaskMemFree(text); }
		};

		using DialogOwner = std::unique_ptr<IFileDialog, ComRelease>;
		using ShellItemOwner = std::unique_ptr<IShellItem, ComRelease>;
		using CoTaskTextOwner = std::unique_ptr<wchar_t, CoTaskMemDelete>;

		HRESULT AddOptions(IFileDialog& dialog, FILEOPENDIALOGOPTIONS addedOptions)
		{
			DWORD currentOptions = 0;
			const HRESULT readStatus = dialog.GetOptions(&currentOptions);

			if (FAILED(readStatus))
			{
				return readStatus;
			}

			return dialog.SetOptions(currentOptions | addedOptions);
		}

		// Where a save dialog starts: the folder it opens in and the name filled in.
		struct SaveStart
		{
			std::filesystem::path Folder;
			std::string FileName;
		};

		Result<std::filesystem::path> PickWithDialog(REFCLSID dialogClass, FILEOPENDIALOGOPTIONS pickerOptions, const std::string& title,
													 const std::vector<FileDialog::Filter>& filters, const std::string& whatIsPicked,
													 const SaveStart* saveStart = nullptr)
		{
			const ComGuard comGuard;
			if (!comGuard.IsUsable())
			{
				return Error{.Code = ErrorCode::Unsupported, .Message = "COM initialisation failed"};
			}

			IFileDialog* createdDialog = nullptr;
			const HRESULT createStatus = ::CoCreateInstance(dialogClass, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&createdDialog));

			if (FAILED(createStatus) || createdDialog == nullptr)
			{
				return Error{.Code = ErrorCode::Unsupported, .Message = "cannot create the system " + whatIsPicked + " dialog"};
			}

			const DialogOwner dialog{createdDialog};

			const HRESULT configureStatus = AddOptions(*dialog, pickerOptions);

			if (FAILED(configureStatus))
			{
				return Error{.Code = ErrorCode::Unsupported, .Message = "cannot configure the dialog as a " + whatIsPicked + " picker"};
			}

			// Each COMDLG_FILTERSPEC holds raw pointers into these strings, and a short wstring keeps
			// its characters inside itself, where a reallocation would move them. So the reserve is
			// load-bearing: exactly one push_back per reserved slot, and no reallocation.
			std::vector<std::wstring> filterStrings;
			filterStrings.reserve(filters.size() * kStringsPerFilter);

			std::vector<COMDLG_FILTERSPEC> filterSpecs;
			filterSpecs.reserve(filters.size());

			for (const FileDialog::Filter& filter : filters)
			{
				filterStrings.push_back(ToUtf16(filter.Label));
				filterStrings.push_back(ToUtf16(filter.Pattern));

				const std::wstring& label = filterStrings[filterStrings.size() - kStringsPerFilter];
				const std::wstring& pattern = filterStrings.back();
				filterSpecs.push_back(COMDLG_FILTERSPEC{.pszName = label.c_str(), .pszSpec = pattern.c_str()});
			}

			if (!filterSpecs.empty())
			{
				dialog->SetFileTypes(static_cast<UINT>(filterSpecs.size()), filterSpecs.data());
				dialog->SetFileTypeIndex(kFirstFilterCountingFromOne);
			}

			if (saveStart != nullptr)
			{
				if (!saveStart->FileName.empty())
				{
					dialog->SetFileName(ToUtf16(saveStart->FileName).c_str());
				}

				IShellItem* createdFolder = nullptr;

				if (!saveStart->Folder.empty() &&
					SUCCEEDED(::SHCreateItemFromParsingName(saveStart->Folder.c_str(), nullptr, IID_PPV_ARGS(&createdFolder))) &&
					createdFolder != nullptr)
				{
					const ShellItemOwner folder{createdFolder};
					dialog->SetFolder(folder.get());
				}
			}

			const std::wstring wideTitle = ToUtf16(title);
			if (!wideTitle.empty())
			{
				dialog->SetTitle(wideTitle.c_str());
			}

			const HRESULT showStatus = dialog->Show(kNoOwnerWindow);
			const bool userCancelled = showStatus == HRESULT_FROM_WIN32(ERROR_CANCELLED);

			if (userCancelled)
			{
				return std::filesystem::path{};
			}

			if (FAILED(showStatus))
			{
				return Error{.Code = ErrorCode::IoFailure, .Message = "the " + whatIsPicked + " dialog failed"};
			}

			IShellItem* createdSelection = nullptr;
			const HRESULT resultStatus = dialog->GetResult(&createdSelection);

			if (FAILED(resultStatus) || createdSelection == nullptr)
			{
				return Error{.Code = ErrorCode::IoFailure, .Message = "the " + whatIsPicked + " dialog returned no selection"};
			}

			const ShellItemOwner selection{createdSelection};

			PWSTR createdPathText = nullptr;
			const HRESULT nameStatus = selection->GetDisplayName(SIGDN_FILESYSPATH, &createdPathText);

			if (FAILED(nameStatus) || createdPathText == nullptr)
			{
				return Error{.Code = ErrorCode::IoFailure, .Message = "the selected item has no filesystem path"};
			}

			const CoTaskTextOwner selectedPathText{createdPathText};
			std::filesystem::path picked{selectedPathText.get()};

			if (picked.empty())
			{
				return Error{.Code = ErrorCode::IoFailure, .Message = "the selected item has no filesystem path"};
			}

			return picked;
		}
	}
#endif

	Result<std::filesystem::path> FileDialog::PickFolder([[maybe_unused]] const std::string& title)
	{
#ifdef CORE_PLATFORM_WINDOWS
		const std::vector<Filter> noFilters;
		return PickWithDialog(CLSID_FileOpenDialog, FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST, title, noFilters,
							  "folder");
#else
		return Error{.Code = ErrorCode::Unsupported,
					 .Message = "a native folder picker is not implemented on this platform"};
#endif
	}

	Result<std::filesystem::path> FileDialog::PickFile([[maybe_unused]] const std::string& title,
													   [[maybe_unused]] const std::vector<Filter>& filters)
	{
#ifdef CORE_PLATFORM_WINDOWS
		return PickWithDialog(CLSID_FileOpenDialog, FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_FILEMUSTEXIST, title, filters,
							  "file");
#else
		return Error{.Code = ErrorCode::Unsupported,
					 .Message = "a native file picker is not implemented on this platform"};
#endif
	}

	Result<std::filesystem::path> FileDialog::PickSaveFile([[maybe_unused]] const std::string& title,
														   [[maybe_unused]] const std::filesystem::path& suggested)
	{
#ifdef CORE_PLATFORM_WINDOWS
		const std::vector<Filter> noFilters;
		const SaveStart start{.Folder = suggested.parent_path(), .FileName = ToUtf8(suggested.filename())};

		return PickWithDialog(CLSID_FileSaveDialog, FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_OVERWRITEPROMPT, title, noFilters,
							  "save", &start);
#else
		return Error{.Code = ErrorCode::Unsupported,
					 .Message = "a native save dialog is not implemented on this platform"};
#endif
	}
}
