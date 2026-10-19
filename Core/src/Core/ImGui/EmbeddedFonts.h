#pragma once

namespace Core {

	// A font compiled into the executable, in the form ImGui's AddFontFromMemoryCompressedTTF reads.
	// The data is static and lives as long as the program.
	struct EmbeddedFont
	{
		const unsigned char* CompressedData;
		int CompressedSize;
	};

	EmbeddedFont JetBrainsMonoRegular();
	EmbeddedFont JetBrainsMonoBold();
	EmbeddedFont Lucide();
	EmbeddedFont InterRegular();
	EmbeddedFont InterSemiBold();
}
