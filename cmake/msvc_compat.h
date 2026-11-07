/* MSVC has no __attribute__: the C libraries that use it (shine) only use it for hints. */
#pragma once
#ifdef _MSC_VER
 #define __attribute__(x)
#endif
