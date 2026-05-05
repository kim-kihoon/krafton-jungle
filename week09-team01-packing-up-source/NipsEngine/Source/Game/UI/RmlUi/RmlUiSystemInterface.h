#pragma once

#include "RmlUi/Core/SystemInterface.h"

class FRmlUiSystemInterface : public Rml::SystemInterface
{
public:
	FRmlUiSystemInterface();

	double GetElapsedTime() override;
	bool LogMessage(Rml::Log::Type Type, const Rml::String& Message) override;

private:
	double SecondsPerCount = 0.0;
	long long StartCounter = 0;
};
