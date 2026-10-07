#pragma once
#include <string>
// Maps the owner's PE data, binds imports, tests lifted code and attempts CRT
// entry until the first unsupported host service. Returns an honest status.
std::string connectGuestRuntime(const char* executable);
void requestGuestRuntimeStop();
void setGuestDisplaySize(unsigned width,unsigned height);
void setGuestKey(unsigned scan,bool down);
void clearGuestInput();
void setGuestLanguage(const char* language);
