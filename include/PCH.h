#pragma once

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <SimpleIni.h>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/msvc_sink.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <chrono>
#include <cmath>
#include <filesystem>

using namespace std::literals;

namespace logger = SKSE::log;

namespace util
{
	using SKSE::stl::report_and_fail;
}

#define DLLEXPORT __declspec(dllexport)

#include "Plugin.h"
