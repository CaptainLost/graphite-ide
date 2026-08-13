#include "corepch.h"

#include "Core/Platform/FileDialog.h"

#ifdef CORE_PLATFORM_WINDOWS
#include <windows.h>

#include <shobjidl.h>
#endif

namespace Core {

#ifdef CORE_PLATFORM_WINDOWS
	namespace {
		class ComGuard
		{
		public:
			ComGuard()
			{
				const HRESULT result = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED |
																	 COINIT_DISABLE_OLE1DDE);
				m_Owned = SUCCEEDED(result);
				m_Ok = m_Owned || result == RPC_E_CHANGED_MODE;
			}

			~ComGuard()
			{
				if (m_Owned)
					::CoUninitialize();
			}

			ComGuard(const ComGuard&) = delete;
			ComGuard& operator=(const ComGuard&) = delete;

			bool Ok() const { return m_Ok; }

		private:
			bool m_Owned = false;
			bool m_Ok = false;
		};

		std::wstring Widen(const std::string& text)
		{
			if (text.empty())
				return {};

			const int length = ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
													 static_cast<int>(text.size()), nullptr, 0);
			std::wstring wide(static_cast<std::size_t>(length), L'\0');
			::MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
								  wide.data(), length);
			return wide;
		}
	}
#endif

	Result<std::filesystem::path> FileDialog::PickFolder(const std::string& title)
	{
#ifdef CORE_PLATFORM_WINDOWS
		const ComGuard com;
		if (!com.Ok())
			return Error{ErrorCode::Unsupported, "COM initialisation failed"};

		IFileOpenDialog* dialog = nullptr;
		HRESULT result = ::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
											IID_PPV_ARGS(&dialog));

		if (FAILED(result) || dialog == nullptr)
			return Error{ErrorCode::Unsupported, "cannot create the system folder dialog"};

		DWORD options = 0;
		if (FAILED(dialog->GetOptions(&options)) ||
			FAILED(dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST)))
		{
			dialog->Release();
			return Error{ErrorCode::Unsupported, "cannot configure the dialog as a folder picker"};
		}

		const std::wstring wideTitle = Widen(title);
		if (!wideTitle.empty())
			dialog->SetTitle(wideTitle.c_str());

		result = dialog->Show(nullptr);

		if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED))
		{
			dialog->Release();
			return std::filesystem::path{};
		}

		if (FAILED(result))
		{
			dialog->Release();
			return Error{ErrorCode::IoFailure, "the folder dialog failed"};
		}

		IShellItem* item = nullptr;
		result = dialog->GetResult(&item);

		if (FAILED(result) || item == nullptr)
		{
			dialog->Release();
			return Error{ErrorCode::IoFailure, "the folder dialog returned no selection"};
		}

		PWSTR selected = nullptr;
		result = item->GetDisplayName(SIGDN_FILESYSPATH, &selected);

		std::filesystem::path picked;
		if (SUCCEEDED(result) && selected != nullptr)
		{
			picked = selected;
			::CoTaskMemFree(selected);
		}

		item->Release();
		dialog->Release();

		if (picked.empty())
			return Error{ErrorCode::IoFailure, "the selected item has no filesystem path"};

		return picked;
#else
		(void)title;

		return Error{ErrorCode::Unsupported,
					 "a native folder picker is not implemented on this platform"};
#endif
	}

	Result<std::filesystem::path> FileDialog::PickFile(const std::string& title,
													   const std::vector<Filter>& filters)
	{
#ifdef CORE_PLATFORM_WINDOWS
		const ComGuard com;
		if (!com.Ok())
			return Error{ErrorCode::Unsupported, "COM initialisation failed"};

		IFileOpenDialog* dialog = nullptr;
		HRESULT result = ::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
											IID_PPV_ARGS(&dialog));

		if (FAILED(result) || dialog == nullptr)
			return Error{ErrorCode::Unsupported, "cannot create the system file dialog"};

		DWORD options = 0;
		if (FAILED(dialog->GetOptions(&options)) ||
			FAILED(dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_FILEMUSTEXIST)))
		{
			dialog->Release();
			return Error{ErrorCode::Unsupported, "cannot configure the dialog as a file picker"};
		}

		// The COMDLG_FILTERSPEC entries below hold raw pointers into these strings, and moving
		// a short wstring copies its buffer inline, so the reserve is load-bearing rather than
		// a performance hint: exactly one push_back per reserved slot, and no reallocation.
		std::vector<std::wstring> storage;
		storage.reserve(filters.size() * 2);

		std::vector<COMDLG_FILTERSPEC> specs;
		specs.reserve(filters.size());

		for (const Filter& filter : filters)
		{
			storage.push_back(Widen(filter.Label));
			storage.push_back(Widen(filter.Pattern));

			specs.push_back(COMDLG_FILTERSPEC{storage[storage.size() - 2].c_str(),
											  storage.back().c_str()});
		}

		if (!specs.empty())
		{
			dialog->SetFileTypes(static_cast<UINT>(specs.size()), specs.data());
			dialog->SetFileTypeIndex(1);
		}

		const std::wstring wideTitle = Widen(title);
		if (!wideTitle.empty())
			dialog->SetTitle(wideTitle.c_str());

		result = dialog->Show(nullptr);

		if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED))
		{
			dialog->Release();
			return std::filesystem::path{};
		}

		if (FAILED(result))
		{
			dialog->Release();
			return Error{ErrorCode::IoFailure, "the file dialog failed"};
		}

		IShellItem* item = nullptr;
		result = dialog->GetResult(&item);

		if (FAILED(result) || item == nullptr)
		{
			dialog->Release();
			return Error{ErrorCode::IoFailure, "the file dialog returned no selection"};
		}

		PWSTR selected = nullptr;
		result = item->GetDisplayName(SIGDN_FILESYSPATH, &selected);

		std::filesystem::path picked;
		if (SUCCEEDED(result) && selected != nullptr)
		{
			picked = selected;
			::CoTaskMemFree(selected);
		}

		item->Release();
		dialog->Release();

		if (picked.empty())
			return Error{ErrorCode::IoFailure, "the selected item has no filesystem path"};

		return picked;
#else
		(void)title;
		(void)filters;

		return Error{ErrorCode::Unsupported,
					 "a native file picker is not implemented on this platform"};
#endif
	}
}