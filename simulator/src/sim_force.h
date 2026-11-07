// The simulator: included before each C file of the firmware (-include; not the C++ files, not simulator/src/). The
// paths of the board ("/data/chess.bin", "/sdcard/apps") go to the data folder of the simulator (sim_fs.c), not to the
// root of the PC disk.
//
// The system headers come first, so that their own declarations keep their names (the stat() of the UCRT is an inline
// function); a declaration in a header that comes later (io.h, direct.h, dirent.h) becomes a declaration of the sim_
// function, with the same types. This header must not take names from the file: _MODE_T_ keeps "mode_t" out (DEAD
// PIXEL has its own mode_t), and the access() macros of io.h (sys/stat.h includes it) go (POUR has its own R_OK).
#pragma once
#ifndef SIM_FS_IMPL
#ifndef _MODE_T_
#define _MODE_T_
typedef unsigned short _mode_t;
#endif
#include "sim_det.h"
#include <stdio.h>
#include <sys/stat.h>
#undef R_OK
#undef W_OK
#undef X_OK
#undef F_OK

FILE *sim_fopen(const char *path, const char *mode);
int sim_stat(const char *path, struct stat *st);
int sim_mkdir(const char *path);
int sim_rmdir(const char *path);
int sim_remove(const char *path);
int sim_unlink(const char *path);
int sim_rename(const char *from, const char *to);

#define fopen(p, m)     sim_fopen(p, m)
#define stat(p, s)      sim_stat(p, s)
#define mkdir(p, ...)   sim_mkdir(p)            // (the board: mkdir(path, mode); the host code of an app: mkdir(path))
#define rmdir(p)        sim_rmdir(p)
#define remove(p)       sim_remove(p)
#define unlink(p)       sim_unlink(p)
#define rename(a, b)    sim_rename(a, b)
#define opendir(p)      sim_opendir(p)          // (dirent.h declares it: DIR *sim_opendir(const char *))
#endif
