#pragma once

#include "Archive.h"
#include "Platform/Paths.h"
#include <cstring>
#include <fstream>
#include <string>
#include <iostream>

class FWindowsBinWriter : public FArchive
{
private:
	std::ofstream FileStream;
	struct FTaggedPropertyWriteRecord
	{
		FString Name;
		TArray<uint8> Payload;
	};
	struct FTaggedObjectWriteScope
	{
		TArray<FTaggedPropertyWriteRecord> Properties;
	};

	TArray<FTaggedObjectWriteScope> TaggedObjectWriteScopes;
	TArray<FString> TaggedPropertyWriteNames;
	TArray<TArray<uint8>> TaggedPropertyWriteBuffers;

public:
	FWindowsBinWriter(const std::string& FilePath)
	{
		bIsSaving = true; // 나는 '쓰기' 전용이다!
		FileStream.open(FPaths::ToWide(FilePath), std::ios::binary);
	}

	~FWindowsBinWriter() override
	{
		if (FileStream.is_open()) FileStream.close();
	}

	// 파일이 정상적으로 열렸는지 확인
	bool IsValid() const { return FileStream.is_open() && FileStream.good(); }

	void BeginObject() override
	{
		if (bUseTaggedPropertySerialization)
		{
			TaggedObjectWriteScopes.emplace_back();
		}
	}

	void EndObject() override
	{
		if (!bUseTaggedPropertySerialization || TaggedObjectWriteScopes.empty())
		{
			return;
		}

		FTaggedObjectWriteScope Scope = std::move(TaggedObjectWriteScopes.back());
		TaggedObjectWriteScopes.pop_back();

		uint32 PropertyCount = static_cast<uint32>(Scope.Properties.size());
		*this << PropertyCount;
		for (FTaggedPropertyWriteRecord& Property : Scope.Properties)
		{
			*this << Property.Name;

			uint32 PayloadSize = static_cast<uint32>(Property.Payload.size());
			*this << PayloadSize;
			if (PayloadSize > 0)
			{
				Serialize(Property.Payload.data(), PayloadSize);
			}
		}
	}

	void BeginProperty(const char* Name) override
	{
		if (!bUseTaggedPropertySerialization)
		{
			return;
		}

		TaggedPropertyWriteNames.push_back(Name ? FString(Name) : FString());
		TaggedPropertyWriteBuffers.emplace_back();
	}

	void EndProperty() override
	{
		if (!bUseTaggedPropertySerialization
			|| TaggedObjectWriteScopes.empty()
			|| TaggedPropertyWriteBuffers.empty()
			|| TaggedPropertyWriteNames.empty())
		{
			return;
		}

		FTaggedPropertyWriteRecord Record;
		Record.Name = std::move(TaggedPropertyWriteNames.back());
		TaggedPropertyWriteNames.pop_back();
		Record.Payload = std::move(TaggedPropertyWriteBuffers.back());
		TaggedPropertyWriteBuffers.pop_back();

		TaggedObjectWriteScopes.back().Properties.push_back(std::move(Record));
	}

	void Serialize(void* Data, size_t Num) override
	{
		if (Num == 0)
		{
			return;
		}

		if (bUseTaggedPropertySerialization && !TaggedPropertyWriteBuffers.empty())
		{
			TArray<uint8>& Buffer = TaggedPropertyWriteBuffers.back();
			const size_t OldSize = Buffer.size();
			Buffer.resize(OldSize + Num);
			std::memcpy(Buffer.data() + OldSize, Data, Num);
			return;
		}

		if (FileStream.is_open())
		{
			FileStream.write(static_cast<const char*>(Data), Num);
		}
	}
};

class FWindowsBinReader : public FArchive
{
private:
	std::ifstream FileStream;
	struct FTaggedObjectReadScope
	{
		TMap<FString, TArray<uint8>> Properties;
	};
	struct FTaggedPropertyReadState
	{
		TArray<uint8> Payload;
		size_t Offset = 0;
	};

	TArray<FTaggedObjectReadScope> TaggedObjectReadScopes;
	TArray<FTaggedPropertyReadState> TaggedPropertyReadStack;

public:
	FWindowsBinReader(const std::string& FilePath)
	{
		bIsLoading = true; // 나는 '읽기' 전용이다!
		FileStream.open(FPaths::ToWide(FilePath), std::ios::binary);
	}

	~FWindowsBinReader() override
	{
		if (FileStream.is_open()) FileStream.close();
	}

	bool IsValid() const { return FileStream.is_open() && FileStream.good(); }

	void BeginObject() override
	{
		if (!bUseTaggedPropertySerialization)
		{
			return;
		}

		FTaggedObjectReadScope Scope;
		uint32 PropertyCount = 0;
		*this << PropertyCount;

		for (uint32 PropertyIndex = 0; PropertyIndex < PropertyCount; ++PropertyIndex)
		{
			FString PropertyName;
			*this << PropertyName;

			uint32 PayloadSize = 0;
			*this << PayloadSize;

			TArray<uint8> Payload;
			Payload.resize(PayloadSize);
			if (PayloadSize > 0)
			{
				Serialize(Payload.data(), PayloadSize);
			}

			Scope.Properties[PropertyName] = std::move(Payload);
		}

		TaggedObjectReadScopes.push_back(std::move(Scope));
	}

	void EndObject() override
	{
		if (bUseTaggedPropertySerialization && !TaggedObjectReadScopes.empty())
		{
			TaggedObjectReadScopes.pop_back();
		}
	}

	bool HasProperty(const char* Name) const override
	{
		if (!bUseTaggedPropertySerialization)
		{
			return true;
		}

		if (!Name || TaggedObjectReadScopes.empty())
		{
			return false;
		}

		const FTaggedObjectReadScope& Scope = TaggedObjectReadScopes.back();
		return Scope.Properties.find(FString(Name)) != Scope.Properties.end();
	}

	void BeginProperty(const char* Name) override
	{
		if (!bUseTaggedPropertySerialization)
		{
			return;
		}

		FTaggedPropertyReadState State;
		if (Name && !TaggedObjectReadScopes.empty())
		{
			const FTaggedObjectReadScope& Scope = TaggedObjectReadScopes.back();
			auto It = Scope.Properties.find(FString(Name));
			if (It != Scope.Properties.end())
			{
				State.Payload = It->second;
			}
		}

		TaggedPropertyReadStack.push_back(std::move(State));
	}

	void EndProperty() override
	{
		if (bUseTaggedPropertySerialization && !TaggedPropertyReadStack.empty())
		{
			TaggedPropertyReadStack.pop_back();
		}
	}

	void Serialize(void* Data, size_t Num) override
	{
		if (Num == 0)
		{
			return;
		}

		if (bUseTaggedPropertySerialization && !TaggedPropertyReadStack.empty())
		{
			FTaggedPropertyReadState& State = TaggedPropertyReadStack.back();
			if (State.Offset + Num > State.Payload.size())
			{
				std::memset(Data, 0, Num);
				State.Offset = State.Payload.size();
				return;
			}

			std::memcpy(Data, State.Payload.data() + State.Offset, Num);
			State.Offset += Num;
			return;
		}

		if (FileStream.is_open())
		{
			FileStream.read(static_cast<char*>(Data), Num);
		}
	}
};
