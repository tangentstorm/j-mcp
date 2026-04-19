/* Dynamic loader for libj. Mirrors the jsource private `jeload` helper but
 * lives inside j-mcp so we do not depend on a jsource-private symbol. */
#ifndef JMCP_JLIB_H
#define JMCP_JLIB_H

#include <stddef.h>
#include <stdint.h>

/* Output-type values from jsrc/jlib.h. Kept local so we don't need the
 * jsource headers at build time. */
#define JMCP_MTYOFM   1   /* formatted result array output */
#define JMCP_MTYOER   2   /* error output */
#define JMCP_MTYOLOG  3   /* log */
#define JMCP_MTYOSYS  4   /* system assertion failure */
#define JMCP_MTYOEXIT 5   /* exit */
#define JMCP_MTYOFILE 6   /* output of 1!:2[2 */

/* smoptions values. */
#define JMCP_SMCON    3
#define JMCP_SMOPTMTH 8

/* Opaque pointer to J interpreter state. */
typedef void *JS;

/* Callback types the host supplies to JSM. */
typedef void (*j_output_fn)(JS jt, int type, char *s);
typedef int  (*j_wd_fn)(JS jt, int x, void *parg, void **pres, char *loc);
typedef char *(*j_input_fn)(JS jt, char *prompt);

/* Load libj once. Returns 0 on success, -1 on failure (sets *err to a
 * static message). `libpath` is an explicit path or NULL to try the default
 * name `libj.so` via the linker search order. */
int jlib_load(const char *libpath, const char **err);

/* Create a new J instance and register callbacks. Returns NULL on failure. */
JS   jlib_new(j_output_fn out, j_wd_fn wd, j_input_fn in, int smoptions);

/* Thin wrappers around the resolved libj symbols. */
int  jlib_do(JS jt, const char *sentence);
void jlib_interrupt(JS jt);
void jlib_free(JS jt);
const char *jlib_get_locale(JS jt);

/* JGetM / JSetM: binary array exchange. `jtype` is a J type code (B01=1,
 * LIT=2, INT=4, FL=8, ...). On get, `jshape` and `jdata` receive pointers
 * into J's memory (cast as int64_t); valid until the next J call.
 * Returns 0 on success, else a J error code. */
int jlib_get_m(JS jt, const char *name,
               int64_t *jtype, int64_t *jrank,
               int64_t *jshape, int64_t *jdata);

int jlib_set_m(JS jt, const char *name,
               int64_t *jtype, int64_t *jrank,
               int64_t *jshape, int64_t *jdata);

/* J noun-type constants (from jsrc/jtype.h). */
#define JMCP_JB01  1           /* boolean, 1 byte */
#define JMCP_JLIT  2           /* character (byte), 1 byte */
#define JMCP_JINT  4           /* int64, 8 bytes */
#define JMCP_JFL   8           /* double, 8 bytes */
#define JMCP_JC2T  (1LL<<17)   /* 2-byte unicode char (UTF-16 code unit) */
#define JMCP_JC4T  (1LL<<18)   /* 4-byte unicode char (codepoint) */

/* Path to the loaded libj (for diagnostics). NULL before jlib_load. */
const char *jlib_loaded_path(void);

#endif
