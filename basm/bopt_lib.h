/* bopt_lib — BASM optimizer library (linked into basm) */
#ifndef BOPT_LIB_H
#define BOPT_LIB_H
/* Optimize BASM text in-place file: read path, write path (can be same).
   Returns 0 on success. level 0=noop 1=light 2=normal 3=aggressive */
int bopt_file(const char *inpath, const char *outpath, int level);
#endif
