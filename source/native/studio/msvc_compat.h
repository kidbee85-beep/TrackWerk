// Force-included into pt-mutable on MSVC only (studio/CMakeLists.txt). cl.exe's /D cannot define a
// function-like macro, and the Mutable firmware code uses GCC's __attribute__((always_inline)).
#pragma once
#include <cstdio>   // Plaits' TEST helpers call printf (GCC gets this via -include cstdio)
#define __attribute__(x)
