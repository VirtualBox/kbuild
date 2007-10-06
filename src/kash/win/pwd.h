#ifndef __pwd_h__
#define __pwd_h__
#include <time.h>

struct passwd {
	char	*pw_name;		/* user name */
	char	*pw_passwd;		/* encrypted password */
	int	pw_uid;			/* user uid */
	int	pw_gid;			/* user gid */
	time_t	pw_change;		/* password change time */
	char	*pw_class;		/* user access class */
	char	*pw_gecos;		/* Honeywell login info */
	char	*pw_dir;		/* home directory */
	char	*pw_shell;		/* default shell */
	time_t	pw_expire;		/* account expiration */
	int	pw_fields;		/* internal: fields filled in */
};

struct passwd	*getpwnam(const char *);
struct passwd	*getpwuid(int);


#endif
