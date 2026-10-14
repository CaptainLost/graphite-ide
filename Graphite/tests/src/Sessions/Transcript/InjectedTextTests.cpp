#include <doctest/doctest.h>

#include "Sessions/Transcript/InjectedText.h"

using namespace Graphite;

TEST_CASE("Text Claude Code injects is recognised by its opening tag")
{
	for (const char* text : {"<task-notification>\n<task-id>x</task-id>", "<command-name>/clear</command-name>",
							 "<command-message>clear</command-message>", "<local-command-stdout>ok</local-command-stdout>",
							 "<system-reminder>be brief</system-reminder>", "<user-prompt-submit-hook>x</user-prompt-submit-hook>",
							 "  \n<task-notification>late</task-notification>"})
	{
		CHECK(IsInjectedText(text));
	}
}

TEST_CASE("A prompt that merely contains or even starts with markup is still a prompt")
{
	CHECK_FALSE(IsInjectedText("Why is <task-notification> shown?"));
	CHECK_FALSE(IsInjectedText("<div> does not render"));
	CHECK_FALSE(IsInjectedText(""));
}

TEST_CASE("A task notification is labelled by its summary")
{
	const char* text = "<task-notification>\n<task-id>b1</task-id>\n<status>failed</status>\n"
					   "<summary>Background command \"Build\" failed with exit code 4</summary>\n</task-notification>";

	CHECK(InjectedLabel(text) == "Background command \"Build\" failed with exit code 4");
}

TEST_CASE("Other injected text is labelled by what it is")
{
	CHECK(InjectedLabel("<command-name>/clear</command-name>") == "Command /clear");
	CHECK(InjectedLabel("<local-command-stdout>ok</local-command-stdout>") == "Command output");
	CHECK(InjectedLabel("<system-reminder>x</system-reminder>") == "System reminder");
	CHECK(InjectedLabel("<task-notification><task-id>x</task-id></task-notification>") == "Task notification");
}

TEST_CASE("An interruption is injected text, labelled as one")
{
	for (const char* text : {"[Request interrupted by user]", "[Request interrupted by user for tool use]",
							 "  \n[Request interrupted by user]"})
	{
		CHECK(IsInjectedText(text));
		CHECK(InjectedLabel(text) == "Interrupted by user");
	}

	CHECK_FALSE(IsInjectedText("Why did I see [Request interrupted by user]?"));
}

TEST_CASE("A pasted image's source line is labelled by the image's file name")
{
	CHECK(FirstLineLabel("[Image: source: C:\\Users\\m\\.claude\\image-cache\\images\\17.png]", 120) ==
		  "Image source: 17.png");
	CHECK(FirstLineLabel("[Image: source: /home/m/images/shot.jpg]\nmore", 120) == "Image source: shot.jpg");
}

TEST_CASE("A first line that fits is its own label, and one that does not is cut with an ellipsis")
{
	CHECK(FirstLineLabel("Base directory for this skill\nmore", 120) == "Base directory for this skill");
	CHECK(FirstLineLabel(std::string(120, 'a'), 120) == std::string(120, 'a'));
	CHECK(FirstLineLabel(std::string(200, 'a'), 120) == std::string(117, 'a') + "\xe2\x80\xa6");
}

TEST_CASE("A label is cut before a character, never inside one")
{
	constexpr int kCharactersInText = 100;
	constexpr int kCharactersThatFitBeforeEllipsis = 58;

	std::string text;
	for (int characterIndex = 0; characterIndex < kCharactersInText; ++characterIndex)
	{
		text += "\xc5\xbc";
	}

	std::string expected;
	for (int characterIndex = 0; characterIndex < kCharactersThatFitBeforeEllipsis; ++characterIndex)
	{
		expected += "\xc5\xbc";
	}

	CHECK(FirstLineLabel(text, 120) == expected + "\xe2\x80\xa6");
}
