// Calls each AGENTS.md-banned string function once, except `gets` --
// C++14 removed it, so a C++20 translation unit cannot call it. Half the
// calls go through `std::`, half unqualified: CustomFunctions must match
// both spellings for the ban to hold. clang-tidy on this file with the
// repo's .clang-tidy must report all eight; a silent miss means a regex
// typo turned the ban off. Outside every CMake target and every directory
// .clang-tidy's HeaderFilterRegex or the workflow's tree run scans.
#include <cstdarg>
#include <cstdio>
#include <cstring>

static void vararg_probe(const char *fmt, ...) {
  char buf[64];
  va_list ap1;
  va_start(ap1, fmt);
  std::vsprintf(buf, fmt, ap1);
  va_end(ap1);

  va_list ap2;
  va_start(ap2, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap2);
  va_end(ap2);
}

static void probe() {
  char buf[64] = {};

  std::sprintf(buf, "%d", 1);
  snprintf(buf, sizeof(buf), "%d", 1);

  std::strcpy(buf, "x");
  strncpy(buf, "x", sizeof(buf));

  std::strcat(buf, "y");
  strncat(buf, "y", sizeof(buf) - strlen(buf) - 1);

  vararg_probe("%d", 1);
}
