#define A "p1.h"
#include A

#ifdef C
  #define m(a) <a##_pre.c>
#else
  #define m(a) <a##_post.c>
#endif
#include m(abc)  // <abc_post.c>

