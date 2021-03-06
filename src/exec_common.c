/*
 * Copyright (c) 2009-2015 Todd C. Miller <Todd.Miller@courtesan.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <config.h>

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef HAVE_STRING_H
# include <string.h>
#endif /* HAVE_STRING_H */
#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif /* HAVE_STRINGS_H */
#include <unistd.h>
#ifdef HAVE_PRIV_SET
# include <priv.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <signal.h>

#include "sudo.h"
#include "sudo_exec.h"

/*
 * Disable execution of child processes in the command we are about
 * to run.  On systems with privilege sets, we can remove the exec
 * privilege.  On other systems we use LD_PRELOAD and the like.
 */
static char * const *
disable_execute(char *const envp[])
{
#ifdef _PATH_SUDO_NOEXEC
    char *preload = NULL, **nenvp = NULL;
    int env_len, env_size;
    int preload_idx = -1;
# ifdef RTLD_PRELOAD_ENABLE_VAR
    bool enabled = false;
# endif
#endif /* _PATH_SUDO_NOEXEC */
    debug_decl(disable_execute, SUDO_DEBUG_UTIL)

#ifdef HAVE_PRIV_SET
    /* Solaris privileges, remove PRIV_PROC_EXEC post-execve. */
    (void)priv_set(PRIV_ON, PRIV_INHERITABLE, "PRIV_FILE_DAC_READ", NULL);
    (void)priv_set(PRIV_ON, PRIV_INHERITABLE, "PRIV_FILE_DAC_WRITE", NULL);
    (void)priv_set(PRIV_ON, PRIV_INHERITABLE, "PRIV_FILE_DAC_SEARCH", NULL);
    if (priv_set(PRIV_OFF, PRIV_LIMIT, "PRIV_PROC_EXEC", NULL) == 0)
	debug_return_const_ptr(envp);
    sudo_warn(U_("unable to remove PRIV_PROC_EXEC from PRIV_LIMIT"));
#endif /* HAVE_PRIV_SET */

#ifdef _PATH_SUDO_NOEXEC
    /*
     * Preload a noexec file.  For a list of LD_PRELOAD-alikes, see
     * http://www.fortran-2000.com/ArnaudRecipes/sharedlib.html
     * XXX - need to support 32-bit and 64-bit variants
     */

    /* Count entries in envp, looking for LD_PRELOAD as we go. */
    for (env_len = 0; envp[env_len] != NULL; env_len++) {
	if (strncmp(envp[env_len], RTLD_PRELOAD_VAR "=", sizeof(RTLD_PRELOAD_VAR)) == 0) {
	    preload_idx = env_len;
	    continue;
	}
#ifdef RTLD_PRELOAD_ENABLE_VAR
	if (strncmp(envp[env_len], RTLD_PRELOAD_ENABLE_VAR "=", sizeof(RTLD_PRELOAD_ENABLE_VAR)) == 0) {
	    enabled = true;
	    continue;
	}
#endif
    }

    /* Make a new copy of envp as needed. */
    env_size = env_len + 1 + (preload_idx == -1);
#ifdef RTLD_PRELOAD_ENABLE_VAR
    if (!enabled)
	env_size++;
#endif
    nenvp = reallocarray(NULL, env_size, sizeof(*envp));
    if (nenvp == NULL)
	sudo_fatalx(U_("%s: %s"), __func__, U_("unable to allocate memory"));
    memcpy(nenvp, envp, env_len * sizeof(*envp));
    nenvp[env_len] = NULL;

    /* Prepend our LD_PRELOAD to existing value or add new entry at the end. */
    if (preload_idx == -1) {
# ifdef RTLD_PRELOAD_DEFAULT
	asprintf(&preload, "%s=%s%s%s", RTLD_PRELOAD_VAR, sudo_conf_noexec_path(), RTLD_PRELOAD_DELIM, RTLD_PRELOAD_DEFAULT);
# else
	preload = sudo_new_key_val(RTLD_PRELOAD_VAR, sudo_conf_noexec_path());
# endif
	if (preload == NULL)
	    sudo_fatalx(U_("%s: %s"), __func__, U_("unable to allocate memory"));
	nenvp[env_len++] = preload;
	nenvp[env_len] = NULL;
    } else {
	int len = asprintf(&preload, "%s=%s%s%s", RTLD_PRELOAD_VAR,
	    sudo_conf_noexec_path(), RTLD_PRELOAD_DELIM, nenvp[preload_idx]);
	if (len == -1)
	    sudo_fatalx(U_("%s: %s"), __func__, U_("unable to allocate memory"));
	nenvp[preload_idx] = preload;
    }
# ifdef RTLD_PRELOAD_ENABLE_VAR
    if (!enabled) {
	nenvp[env_len++] = RTLD_PRELOAD_ENABLE_VAR "=";
	nenvp[env_len] = NULL;
    }
# endif

    /* Install new env pointer. */
    envp = nenvp;
#endif /* _PATH_SUDO_NOEXEC */

    debug_return_const_ptr(envp);
}

/*
 * Like execve(2) but falls back to running through /bin/sh
 * ala execvp(3) if we get ENOEXEC.
 */
int
sudo_execve(const char *path, char *const argv[], char *const envp[], bool noexec)
{
    /* Modify the environment as needed to disable further execve(). */
    if (noexec)
	envp = disable_execute(envp);

    execve(path, argv, envp);
    if (errno == ENOEXEC) {
	int argc;
	char **nargv;

	for (argc = 0; argv[argc] != NULL; argc++)
	    continue;
	nargv = reallocarray(NULL, argc + 2, sizeof(char *));
	if (nargv != NULL) {
	    nargv[0] = "sh";
	    nargv[1] = (char *)path;
	    memcpy(nargv + 2, argv + 1, argc * sizeof(char *));
	    execve(_PATH_SUDO_BSHELL, nargv, envp);
	    free(nargv);
	}
    }
    return -1;
}
