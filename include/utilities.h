// utilities.h
#ifndef UTILITIES_H
#define UTILITIES_H

#include <string>
#include <vector>
#include <fstream>

std::string ReadFile(const std::string& path);

// Helper function to convert std::string to wstring for Windows API
std::wstring StringToWString(const std::string& str);

#endif