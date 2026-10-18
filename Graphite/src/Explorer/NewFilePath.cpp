#include "Explorer/NewFilePath.h"

#include "Core/FileSystem/FileName.h"

namespace Graphite {

	std::optional<NewFilePath> ParseNewFilePath(std::string_view typed)
	{
		NewFilePath path;
		std::size_t pieceStart = 0;

		while (true)
		{
			const std::size_t separator = typed.find_first_of("/\\", pieceStart);
			const bool isLastPiece = separator == std::string_view::npos;
			const std::string_view piece = typed.substr(pieceStart, isLastPiece ? std::string_view::npos : separator - pieceStart);

			if (!Core::IsValidFileName(piece))
			{
				return std::nullopt;
			}

			if (isLastPiece)
			{
				path.File = std::string{piece};
				return path;
			}

			path.Folders.emplace_back(piece);
			pieceStart = separator + 1;
		}
	}
}
