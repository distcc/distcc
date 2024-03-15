// Copyright 2024 Martin Pool

// Include the header files currently accessed by Rust, as an input to bindgen.

#include "distcc.h"
#include "exitcode.h"
#include "hosts.h"
#include "implicit.h"
#include "trace.h"
#include "util.h"

// Manually kept in sync with the main declaration in distcc.c.
int distcc_main(int argc, char **argv);
int distccd_main(int argc, char **argv);
