// The C library sys/stat.h for the host test of main/tfn_web.c. The board code calls the POSIX mkdir(path, mode);
// MinGW has only mkdir(path). Its headers come first here, then a macro gives the POSIX form.
#pragma once
#include_next <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define mkdir(path, mode) mkdir(path)
#endif
