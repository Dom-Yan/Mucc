#ifndef __STDBOOL_H
#define __STDBOOL_H

// In C23, bool, true and false are built into mucc; before, they're
// defined here.
#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 202311L
# define bool _Bool
# define true 1
# define false 0
#endif
#define __bool_true_false_are_defined 1

#endif
