#include "jlib.h"
#include "log.h"

#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef JS   (*JInit_t)(void);
typedef void (*JSM_t)(JS, void **);
typedef int  (*JDo_t)(JS, const char *);
typedef void (*JInterrupt_t)(JS);
typedef void (*JFree_t)(JS);
typedef const char *(*JGetLocale_t)(JS);

static void *handle = NULL;
static char  loaded_path[4096];

static JInit_t       p_JInit;
static JSM_t         p_JSM;
static JDo_t         p_JDo;
static JInterrupt_t  p_JInterrupt;
static JFree_t       p_JFree;
static JGetLocale_t  p_JGetLocale;

static int resolve(const char *name, void **slot, const char **err) {
    void *sym = dlsym(handle, name);
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
     *   2. $JHOME/libj.so   (J's conventional install-root variable)
     *   3. sibling of our executable (jconsole-style)
     *   4. bare "libj.so"   (LD_LIBRARY_PATH + system paths)
     */
    const char *candidates[5] = {0};
    int nc = 0;
    char jhome_buf[4096], exe_buf[4096];

    if (libpath && *libpath) candidates[nc++] = libpath;

    const char *jhome = getenv("JHOME");
    if (jhome && *jhome) {
        snprintf(jhome_buf, sizeof jhome_buf, "%s/libj.so", jhome);
        candidates[nc++] = jhome_buf;
    }

    ssize_t n = readlink("/proc/self/exe", exe_buf, sizeof exe_buf - 1);
    if (n > 0) {
        exe_buf[n] = 0;
        char *slash = strrchr(exe_buf, '/');
        if (slash) { strcpy(slash + 1, "libj.so"); candidates[nc++] = exe_buf; }
    }

    candidates[nc++] = "libj.so";

    static char last_err[512];
    last_err[0] = 0;
    for (int i = 0; i < nc; i++) {
        void *h = dlopen(candidates[i], RTLD_LAZY | RTLD_LOCAL);
        if (h) {
            handle = h;
            snprintf(loaded_path, sizeof loaded_path, "%s", candidates[i]);
            break;
        }
        snprintf(last_err, sizeof last_err, "dlopen %s failed: %s",
                 candidates[i], dlerror());
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

const char *jlib_loaded_path(void) {
    return handle ? loaded_path : NULL;
}
