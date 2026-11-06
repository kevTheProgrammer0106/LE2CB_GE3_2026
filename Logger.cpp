#include "Logger.h"

#include <Windows.h>

void Logger::Log(const std::string& message)
{
	OutputDebugStringA(message.c_str());
}