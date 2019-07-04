// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

#define _CRT_SECURE_NO_WARNINGS
#define FLSYS std::experimental::filesystem

#include "targetver.h"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "include/picosha2.h"
#include "include/bit7z.hpp"
#include "include/file_shredder.h"
#include "resource.h"
#include <stdio.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <random>
#include <iomanip>
#include <experimental\filesystem>
#include <thread>
#include <mutex>
#include <atomic>
using FLSYS::path;
using FLSYS::remove;
using FLSYS::is_directory;
using FLSYS::recursive_directory_iterator;
using FLSYS::exists;