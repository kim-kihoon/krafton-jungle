#include "Game/UI/DialoguePanel.h"

#include <algorithm>

std::queue<FDialogueEntry> DialoguePanel::PendingQueue;
std::string DialoguePanel::CurrentSpeaker;
std::string DialoguePanel::CurrentFullText;
int DialoguePanel::VisibleChars = 0;
float DialoguePanel::TypeTimer = 0.f;
bool DialoguePanel::bActive = false;

int DialoguePanel::TotalCharCount(const std::string& Str)
{
	int i = 0;
	int Count = 0;
	while (i < static_cast<int>(Str.size()))
	{
		const unsigned char C = static_cast<unsigned char>(Str[i]);
		if (C < 0x80)
			i += 1;
		else if (C < 0xE0)
			i += 2;
		else if (C < 0xF0)
			i += 3;
		else
			i += 4;
		++Count;
	}
	return Count;
}

int DialoguePanel::CharCountToByteOffset(const std::string& Str, int CharCount)
{
	int i = 0;
	int Chars = 0;
	while (i < static_cast<int>(Str.size()) && Chars < CharCount)
	{
		const unsigned char C = static_cast<unsigned char>(Str[i]);
		if (C < 0x80)
			i += 1;
		else if (C < 0xE0)
			i += 2;
		else if (C < 0xF0)
			i += 3;
		else
			i += 4;
		++Chars;
	}
	return i;
}

void DialoguePanel::StartEntry(const FDialogueEntry& Entry)
{
	CurrentSpeaker = Entry.Speaker;
	CurrentFullText = Entry.Text;
	VisibleChars = 0;
	TypeTimer = 0.f;
	bActive = true;
}

void DialoguePanel::AdvanceQueue()
{
	if (!PendingQueue.empty())
	{
		const FDialogueEntry Next = PendingQueue.front();
		PendingQueue.pop();
		StartEntry(Next);
		return;
	}

	bActive = false;
}

void DialoguePanel::Show(const char* Speaker, const char* Text)
{
	while (!PendingQueue.empty())
		PendingQueue.pop();

	StartEntry({Speaker ? Speaker : "", Text ? Text : ""});
}

void DialoguePanel::Enqueue(const char* Speaker, const char* Text)
{
	const FDialogueEntry Entry{Speaker ? Speaker : "", Text ? Text : ""};
	if (!bActive)
		StartEntry(Entry);
	else
		PendingQueue.push(Entry);
}

void DialoguePanel::Hide()
{
	while (!PendingQueue.empty())
		PendingQueue.pop();

	bActive = false;
	CurrentSpeaker.clear();
	CurrentFullText.clear();
	VisibleChars = 0;
	TypeTimer = 0.f;
}

bool DialoguePanel::IsActive()
{
	return bActive;
}

void DialoguePanel::Tick(float DeltaTime, EUIRenderMode Mode)
{
	(void)Mode;

	if (!bActive)
		return;

	const int TotalChars = TotalCharCount(CurrentFullText);
	if (VisibleChars >= TotalChars)
		return;

	TypeTimer += std::max(0.0f, DeltaTime);
	while (TypeTimer >= CharInterval && VisibleChars < TotalChars)
	{
		TypeTimer -= CharInterval;
		++VisibleChars;
	}
}

bool DialoguePanel::AdvanceOrSkip()
{
	if (!bActive)
		return false;

	const int TotalChars = TotalCharCount(CurrentFullText);
	if (VisibleChars < TotalChars)
	{
		VisibleChars = TotalChars;
		TypeTimer = 0.f;
		return true;
	}

	AdvanceQueue();
	return true;
}

bool DialoguePanel::IsTextComplete()
{
	return !bActive || VisibleChars >= TotalCharCount(CurrentFullText);
}

const std::string& DialoguePanel::GetSpeaker()
{
	return CurrentSpeaker;
}

std::string DialoguePanel::GetVisibleText()
{
	if (!bActive)
		return {};

	const int ByteOffset = CharCountToByteOffset(CurrentFullText, VisibleChars);
	return CurrentFullText.substr(0, ByteOffset);
}

void DialoguePanel::Render(EUIRenderMode Mode)
{
	(void)Mode;
}
