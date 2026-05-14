#pragma once

class IUIInputHandler
{
public:
	virtual ~IUIInputHandler() = default;

	virtual bool OnUIMouseMove(float X, float Y) = 0;
	virtual bool OnUIMouseButtonDown(int Button, float X, float Y) = 0;
	virtual bool OnUIMouseButtonUp(int Button, float X, float Y) = 0;
	virtual bool OnUIKeyDown(int VK) = 0;
	virtual bool OnUIKeyUp(int VK) = 0;
};
