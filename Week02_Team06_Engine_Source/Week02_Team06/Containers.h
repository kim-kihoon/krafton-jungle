#pragma once

template <typename T>
class TArray
{
public:
	T& operator[](uint64 index);
	const T& operator[](uint64 index) const;

	bool IsEmpty();
	void PushBack(T Element);
	void PopBack();
	uint64 Size();
	void SetNum(uint64 NewSize);
	void Clear();
	T* GetData();
	const T* GetData() const;

private:
	std::vector<T> Container;
};

template <typename T, uint64 N>
class TStaticArray
{
public:
	T& operator[](uint64 index);
	const T& operator[](uint64 index) const;

	void fill(uint64 size);

private:
	std::array<T, N> Container;
};

template <typename K, typename V>
class TMap
{
public:
	V& operator[](K Key);
	//cosnt operator[]는 stl에서 지원안해서 없앰

	std::unordered_map<K, V>::iterator begin();
	std::unordered_map<K, V>::iterator end();

	bool IsEmpty();
	V* Find(const K& Key);
	void Insert(const std::pair<K, V> MyPair);
	void Erase(K Key);
	void Clear();

private:
	std::unordered_map<K, V> Container;
	 
};
#include "Containers.inl"
