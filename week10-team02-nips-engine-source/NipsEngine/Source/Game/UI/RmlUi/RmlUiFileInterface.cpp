#include "Game/UI/RmlUi/RmlUiFileInterface.h"
#include <cstdio>
#include <Windows.h>

namespace
{
	std::wstring ToWideString(const std::string& Utf8Str)
	{
		if (Utf8Str.empty()) return {};
		int32_t Size = MultiByteToWideChar(CP_UTF8, 0, Utf8Str.c_str(), -1, nullptr, 0);
		if (Size <= 0) return {};
		std::wstring Result(Size - 1, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, Utf8Str.c_str(), -1, &Result[0], Size);
		return Result;
	}
}

FRmlUiFileInterface::FRmlUiFileInterface()
{
}

FRmlUiFileInterface::~FRmlUiFileInterface()
{
}

Rml::FileHandle FRmlUiFileInterface::Open(const Rml::String& path)
{
	std::wstring wide_path = ToWideString(path);
	FILE* file = nullptr;
	_wfopen_s(&file, wide_path.c_str(), L"rb");
	return reinterpret_cast<Rml::FileHandle>(file);
}

void FRmlUiFileInterface::Close(Rml::FileHandle file)
{
	if (file)
	{
		fclose(reinterpret_cast<FILE*>(file));
	}
}

size_t FRmlUiFileInterface::Read(void* buffer, size_t size, Rml::FileHandle file)
{
	if (!file) return 0;
	return fread(buffer, 1, size, reinterpret_cast<FILE*>(file));
}

bool FRmlUiFileInterface::Seek(Rml::FileHandle file, long offset, int origin)
{
	if (!file) return false;
	return fseek(reinterpret_cast<FILE*>(file), offset, origin) == 0;
}

size_t FRmlUiFileInterface::Tell(Rml::FileHandle file)
{
	if (!file) return 0;
	return ftell(reinterpret_cast<FILE*>(file));
}
