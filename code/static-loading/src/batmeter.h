#pragma once

#ifdef BATMETER_BUILD
#define BATMETER_API __declspec(dllexport)
#else
#define BATMETER_API __declspec(dllimport)
#endif

extern "C" BATMETER_API void RunBatMeter();
