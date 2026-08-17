/*
 * pch.h - host stand-in for the game's precompiled header.
 *
 * check_scripts.py compiles the real NeuronCore/ScriptLex.cpp and
 * ScriptComp.cpp against the real NeuronCore headers; only the two
 * platform headers below the script module are stubbed, so the tool
 * cannot drift from the compiler it is testing.
 */
#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

using UBYTE = unsigned char;
using UWORD = unsigned short;
using UDWORD = unsigned int;
using SBYTE = signed char;
using SWORD = short;
using SDWORD = int;
using UINT = unsigned int;
using BOOL = int;
using STRING = char;

#define SDWORD_MAX 0x7fffffff
#define UBYTE_MAX 0xff

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif
