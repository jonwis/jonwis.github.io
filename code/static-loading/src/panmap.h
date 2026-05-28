#pragma once

#ifdef PANMAP_BUILD
#define PANMAP_API __declspec(dllexport)
#else
#define PANMAP_API __declspec(dllimport)
#endif

extern "C" PANMAP_API void RunPanMap();
