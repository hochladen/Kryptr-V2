// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

#define _CRT_SECURE_NO_WARNINGS

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32) && !defined(__CYGWIN__)
//Windows-specific headers
#include "targetver.h"
#include <Windows.h>
#else
//Linux-specific headers

#endif

#include "resource.h"
#include <stdio.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <random>
#include <iomanip>
#include <filesystem>
#include <thread>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include "include/rapidjson/document.h"
#include "include/rapidjson/writer.h"
#include "include/rapidjson/stringbuffer.h"
#include "include/picosha2.h"
#include "include/bit7z.hpp"
#include "include/file_shredder.h"
#include "include/colors.h"
using std::filesystem::path;
using std::filesystem::remove;
using std::filesystem::is_directory;
using std::filesystem::recursive_directory_iterator;
using std::filesystem::exists;