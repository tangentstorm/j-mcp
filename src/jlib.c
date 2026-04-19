#include "jlib.h"
#include "log.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  #define JDLL_NAME "j.dll"
  /* LOAD_WITH_ALTERED_SEARCH_PATH: resolve the DLL's own dependencies
   * starting from its directory. Without this, j.dll loads but its
   * siblings (jpcre2.dll, libomp, etc.) can't be found and the load fails
   * with a misleading "module could not be found". */
  static void *dl_open(const char *p) {
    return (void *)LoadLibraryExA(p, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
  }
  static void *dl_sym(void *h, const char *s) { return (void *)GetProcAddress((HMODULE)h, s); }
  static const char *dl_err(void) {
    static char buf[256];
    DWORD e = GetLastError();
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, e, 0, buf, sizeof buf, NULL);
    return buf;
  }
#else
  #include <dlfcn.h>
  #include <sys/stat.h>
  #include <unistd.h>
  #if defined(__APPLE__)
    #define JDLL_NAME "libj.dylib"
  #else
    #define JDLL_NAME "libj.so"
  #endif
  static void *dl_open(const char *p) { return dlopen(p, RTLD_LAZY | RTLD_LOCAL); }
  static void *dl_sym(void *h, const char *s) { return dlsym(h, s); }
  static const char *dl_err(void) { const char *e = dlerror(); return e ? e : ""; }
#endif

typedef JS   (*JInit_t)(void);
typedef void (*JSM_t)(JS, void **);
typedef int  (*JDo_t)(JS, const char *);
typedef void (*JInterrupt_t)(JS);
typedef void (*JFree_t)(JS);
typedef const char *(*JGetLocale_t)(JS);
typedef int  (*JGetM_t)(JS, const char *, int64_t *, int64_t *, int64_t *, int64_t *);
typedef int  (*JSetM_t)(JS, const char *, int64_t *, int64_t *, int64_t *, int64_t *);

static void *handle = NULL;
static char  loaded_path[4096];
static char  loaded_dir[4096];

static JInit_t       p_JInit;
static JSM_t         p_JSM;
static JDo_t         p_JDo;
static JInterrupt_t  p_JInterrupt;
static JFree_t       p_JFree;
static JGetLocale_t  p_JGetLocale;
static JGetM_t       p_JGetM;
static JSetM_t       p_JSetM;

static int resolve(const char *name, void **slot, const char **err) {
    void *sym = dl_sym(handle, name);
    if (!sym) {
        static char buf[256];
        snprintf(buf, sizeof buf, "libj missing symbol: %s", name);
        *err = buf;
        return -1;
    }
    *slot = sym;
    return 0;
}

int jlib_load(const char *libpath, const char **err) {
    if (handle) return 0; /* already loaded */

    /* Search order:
     *   1. explicit libpath (from --libj)
     *   2. $JHOME/<libname>   (J's conventional install-root variable)
     *   3. sibling of our executable (jconsole-style)
     *   4. bare library name   (linker search path / LD_LIBRARY_PATH / PATH)
     */
    const char *candidates[5] = {0};
    int nc = 0;
    char jhome_buf[4096], exe_buf[4096];

    if (libpath && *libpath) candidates[nc++] = libpath;

    const char *jhome = getenv("JHOME");
    if (jhome && *jhome) {
#ifdef _WIN32
        snprintf(jhome_buf, sizeof jhome_buf, "%s\\%s", jhome, JDLL_NAME);
#else
        snprintf(jhome_buf, sizeof jhome_buf, "%s/%s", jhome, JDLL_NAME);
#endif
        candidates[nc++] = jhome_buf;
    }

#ifdef _WIN32
    DWORD en = GetModuleFileNameA(NULL, exe_buf, sizeof exe_buf - 1);
    if (en > 0 && en < sizeof exe_buf) {
        exe_buf[en] = 0;
        char *slash = strrchr(exe_buf, '\\');
        if (!slash) slash = strrchr(exe_buf, '/');
        if (slash) { strcpy(slash + 1, JDLL_NAME); candidates[nc++] = exe_buf; }
    }
#else
    ssize_t n = readlink("/proc/self/exe", exe_buf, sizeof exe_buf - 1);
    if (n > 0) {
        exe_buf[n] = 0;
        char *slash = strrchr(exe_buf, '/');
        if (slash) { strcpy(slash + 1, JDLL_NAME); candidates[nc++] = exe_buf; }
    }
#endif

    candidates[nc++] = JDLL_NAME;

    /* Accumulate all failures so the user can see every path we tried. */
    static char last_err[2048];
    last_err[0] = 0;
    size_t elen = 0;
    for (int i = 0; i < nc; i++) {
        void *h = dl_open(candidates[i]);
        if (h) {
            handle = h;
            snprintf(loaded_path, sizeof loaded_path, "%s", candidates[i]);
            /* Derive the directory by stripping the filename. */
            snprintf(loaded_dir, sizeof loaded_dir, "%s", loaded_path);
            char *slash = strrchr(loaded_dir, '/');
            char *bslash = strrchr(loaded_dir, '\\');
            if (bslash && (!slash || bslash > slash)) slash = bslash;
            if (slash) *slash = 0; else loaded_dir[0] = 0;
            break;
        }
        int n = snprintf(last_err + elen, sizeof last_err - elen,
                         "%s[%s: %s]", elen ? "; " : "",
                         candidates[i], dl_err());
        if (n > 0) elen += (size_t)n;
        if (elen >= sizeof last_err) elen = sizeof last_err - 1;
    }
    if (!handle) {
        *err = last_err[0] ? last_err : "dlopen failed";
        return -1;
    }

    if (resolve("JInit",      (void **)&p_JInit,      err) < 0) return -1;
    if (resolve("JSM",        (void **)&p_JSM,        err) < 0) return -1;
    if (resolve("JDo",        (void **)&p_JDo,        err) < 0) return -1;
    if (resolve("JInterrupt", (void **)&p_JInterrupt, err) < 0) return -1;
    if (resolve("JFree",      (void **)&p_JFree,      err) < 0) return -1;
    if (resolve("JGetLocale", (void **)&p_JGetLocale, err) < 0) return -1;
    if (resolve("JGetM",      (void **)&p_JGetM,      err) < 0) return -1;
    if (resolve("JSetM",      (void **)&p_JSetM,      err) < 0) return -1;

    jlog("libj loaded: %s", loaded_path);
    return 0;
}

JS jlib_new(j_output_fn out, j_wd_fn wd, j_input_fn in, int smoptions) {
    if (!handle) { jlog("jlib_new called before jlib_load"); return NULL; }
    JS jt = p_JInit();
    if (!jt) { jlog("JInit returned NULL"); return NULL; }

    /* callbacks[] = { Joutput, Jwd, Jinput, unused, smoptions } */
    void *cbs[5];
    cbs[0] = (void *)(uintptr_t)out;
    cbs[1] = (void *)(uintptr_t)wd;
    cbs[2] = (void *)(uintptr_t)in;
    cbs[3] = NULL;
    cbs[4] = (void *)(uintptr_t)(intptr_t)smoptions;
    p_JSM(jt, cbs);
    return jt;
}

int jlib_do(JS jt, const char *sentence) {
    return p_JDo(jt, sentence);
}

void jlib_interrupt(JS jt) {
    p_JInterrupt(jt);
}

void jlib_free(JS jt) {
    p_JFree(jt);
}

const char *jlib_get_locale(JS jt) {
    return p_JGetLocale(jt);
}

int jlib_get_m(JS jt, const char *name,
               int64_t *jtype, int64_t *jrank,
               int64_t *jshape, int64_t *jdata) {
    return p_JGetM(jt, name, jtype, jrank, jshape, jdata);
}

int jlib_set_m(JS jt, const char *name,
               int64_t *jtype, int64_t *jrank,
               int64_t *jshape, int64_t *jdata) {
    return p_JSetM(jt, name, jtype, jrank, jshape, jdata);
}

const char *jlib_loaded_path(void) {
    return handle ? loaded_path : NULL;
}

const char *jlib_loaded_dir(void) {
    return handle ? loaded_dir : NULL;
}
