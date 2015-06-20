/*
 * Copyright (c) 1999-2005, 2007, 2010-2012, 2014-2015
 *	Todd C. Miller <Todd.Miller@courtesan.com>
 * Copyright (c) 2002 Michael Stroucken <michael@stroucken.org>
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
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Sponsored in part by the Defense Advanced Research Projects
 * Agency (DARPA) and Air Force Research Laboratory, Air Force
 * Materiel Command, USAF, under agreement number F39502-99-1-0512.
 */

#include <config.h>

#ifdef HAVE_SECURID

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef HAVE_STRING_H
# include <string.h>
#endif /* HAVE_STRING_H */
#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif /* HAVE_STRINGS_H */
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif /* HAVE_UNISTD_H */
#include <pwd.h>

/* Needed for SecurID v5.0 Authentication on UNIX */
#define UNIX 1
#include <acexport.h>
#include <sdacmvls.h>

#include "sudoers.h"
#include "sudo_auth.h"

/*
 * securid_init - Initialises communications with ACE server
 * Arguments in:
 *     pw - UNUSED
 *     auth - sudo authentication structure
 *
 * Results out:
 *     auth - auth->data contains pointer to new SecurID handle
 *     return code - Fatal if initialization unsuccessful, otherwise
 *                   success.
 */
int
sudo_securid_init(struct passwd *pw, sudo_auth *auth)
{
    static SDI_HANDLE sd_dat;			/* SecurID handle */
    debug_decl(sudo_securid_init, SUDOERS_DEBUG_AUTH)

    auth->data = (void *) &sd_dat;		/* For method-specific data */

    /* Start communications */
    if (AceInitialize() != SD_FALSE)
	debug_return_int(AUTH_SUCCESS);

    sudo_warnx(U_("failed to initialise the ACE API library"));
    debug_return_int(AUTH_FATAL);
}

/*
 * securid_setup - Initialises a SecurID transaction and locks out other
 *     ACE servers
 *
 * Arguments in:
 *     pw - struct passwd for username
 *     promptp - UNUSED
 *     auth - sudo authentication structure for SecurID handle
 *
 * Results out:
 *     return code - Success if transaction started correctly, fatal
 *                   otherwise
 */
int
sudo_securid_setup(struct passwd *pw, char **promptp, sudo_auth *auth)
{
    SDI_HANDLE *sd = (SDI_HANDLE *) auth->data;
    int retval;
    debug_decl(sudo_securid_setup, SUDOERS_DEBUG_AUTH)

    /* Re-initialize SecurID every time. */
    if (SD_Init(sd) != ACM_OK) {
	sudo_warnx(U_("unable to contact the SecurID server"));
	debug_return_int(AUTH_FATAL);
    }

    /* Lock new PIN code */
    retval = SD_Lock(*sd, pw->pw_name);

    switch (retval) {
	case ACM_OK:
		sudo_warnx(U_("User ID locked for SecurID Authentication"));
		debug_return_int(AUTH_SUCCESS);

        case ACE_UNDEFINED_USERNAME:
		sudo_warnx(U_("invalid username length for SecurID"));
		debug_return_int(AUTH_FATAL);

	case ACE_ERR_INVALID_HANDLE:
		sudo_warnx(U_("invalid Authentication Handle for SecurID"));
		debug_return_int(AUTH_FATAL);

	case ACM_ACCESS_DENIED:
		sudo_warnx(U_("SecurID communication failed"));
		debug_return_int(AUTH_FATAL);

	default:
		sudo_warnx(U_("unknown SecurID error"));
		debug_return_int(AUTH_FATAL);
	}
}

/*
 * securid_verify - Authenticates user and handles ACE responses
 *
 * Arguments in:
 *     pw - struct passwd for username
 *     pass - UNUSED
 *     auth - sudo authentication structure for SecurID handle
 *
 * Results out:
 *     return code - Success on successful authentication, failure on
 *                   incorrect authentication, fatal on errors
 */
int
sudo_securid_verify(struct passwd *pw, char *pass, sudo_auth *auth)
{
    SDI_HANDLE *sd = (SDI_HANDLE *) auth->data;
    int rval;
    debug_decl(sudo_securid_verify, SUDOERS_DEBUG_AUTH)

    pass = auth_getpass("Enter your PASSCODE: ",
	def_passwd_timeout * 60, SUDO_CONV_PROMPT_ECHO_OFF);

    /* Have ACE verify password */
    switch (SD_Check(*sd, pass, pw->pw_name)) {
	case ACM_OK:
		rval = AUTH_SUCESS;
		break;

	case ACE_UNDEFINED_PASSCODE:
		sudo_warnx(U_("invalid passcode length for SecurID"));
		rval = AUTH_FATAL;
		break;

	case ACE_UNDEFINED_USERNAME:
		sudo_warnx(U_("invalid username length for SecurID"));
		rval = AUTH_FATAL;
		break;

	case ACE_ERR_INVALID_HANDLE:
		sudo_warnx(U_("invalid Authentication Handle for SecurID"));
		rval = AUTH_FATAL;
		break;

	case ACM_ACCESS_DENIED:
		rval = AUTH_FAILURE;
		break;

	case ACM_NEXT_CODE_REQUIRED:
                /* Sometimes (when current token close to expire?)
                   ACE challenges for the next token displayed
                   (entered without the PIN) */
        	pass = auth_getpass("\
!!! ATTENTION !!!\n\
Wait for the token code to change, \n\
then enter the new token code.\n", \
		def_passwd_timeout * 60, SUDO_CONV_PROMPT_ECHO_OFF);

		if (SD_Next(*sd, pass) == ACM_OK) {
			rval = AUTH_SUCCESS;
			break;
		}

		rval = AUTH_FAILURE;
		break;

	case ACM_NEW_PIN_REQUIRED:
                /*
		 * This user's SecurID has not been activated yet,
                 * or the pin has been reset
		 */
		/* XXX - Is setting up a new PIN within sudo's scope? */
		SD_Pin(*sd, "");
		fprintf(stderr, "Your SecurID access has not yet been set up.\n");
		fprintf(stderr, "Please set up a PIN before you try to authenticate.\n");
		rval = AUTH_FATAL;
		break;

	default:
		sudo_warnx(U_("unknown SecurID error"));
		rval = AUTH_FATAL;
		break;
    }

    /* Free resources */
    SD_Close(*sd);

    /* Return stored state to calling process */
    debug_return_int(rval);
}

#endif /* HAVE_SECURID */
