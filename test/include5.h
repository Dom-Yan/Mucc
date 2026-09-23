// Looks like an include guard, but isn't: the second block must still be
// read when this file is included again.
#ifndef INCLUDE5_H
#define INCLUDE5_H
#endif
#ifdef INCLUDE5_WANT
#define include5_second 1
#endif
