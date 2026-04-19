#pragma once

class UInput
{
public:
	UInput() = default;
	~UInput() = default;

private:
	UInput(const UInput& rhs) = delete;
	UInput& operator=(const UInput& rhs) = delete;

public:
	bool Initialize();
	void Update();
	void Release();

	bool IsKeyDown(int vKey);
	bool IsKeyPressing(int vKey);
	bool IsKeyUp(int vKey);

	void UpdateMousePosition(POINT MousePos);
	const POINT GetMousePosition() const { return MousePosition; };
	const POINT GetMousePositionDelta() const { return MousePositionDelta; };

	void AddMouseWheelDelta(float Delta) { PendingMouseWheelDelta += Delta; }
	float GetMouseWheelDelta() const { return CurrentMouseWheelDelta; }

public:
	static UInput& GetInstance()
	{
		static UInput Instance;
		return Instance;
	}

private:
	std::array<bool, 256> PreKeys;
	std::array<bool, 256> CurKeys;

	POINT MousePosition = { 0, 0 };
	POINT MousePositionDelta = { 0, 0 };
	POINT PendingMousePositionDelta = { 0, 0 };

	float PendingMouseWheelDelta = 0.0f;
	float CurrentMouseWheelDelta = 0.0f;
};