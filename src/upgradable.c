/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 The FreeBSD Foundation
 *
 * This software was developed by Klara, Inc. under sponsorship from the
 * FreeBSD Foundation and the Sovereign Tech Agency.
 */

#include <getopt.h>
#include <pkg.h>

#include "pkgcli.h"

static int
upgradable(struct pkgdb *db, const char *pattern, match_t match,
    c_charv_t *reponames)
{
	struct pkgdb_it	*it, *rit;
	struct pkg	*pkg = NULL, *rpkg = NULL;
	const char	*name, *ver, *rver, *rrepo;
	int		 cnt = 0;

	/* search for installed packages that match the pattern */
	it = pkgdb_query(db, pattern, match);
	if (it == NULL)
		return (0);

	/* iterate over results */
	while (pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC) == EPKG_OK) {
		/* a locked package is not upgradable */
		if (pkg_is_locked(pkg))
			continue;

		/* get name and version */
		pkg_get(pkg, PKG_ATTR_NAME, &name);
		pkg_get(pkg, PKG_ATTR_VERSION, &ver);

		/* search for name in remote repositories */
		rit = pkgdb_repo_query2(db, name, MATCH_EXACT, reponames);
		if (rit == NULL)
			continue;

		/* iterate over results */
		while (pkgdb_it_next(rit, &rpkg, PKG_LOAD_BASIC) == EPKG_OK) {
			/* get remove version and repo name */
			pkg_get(rpkg, PKG_ATTR_VERSION, &rver);
			pkg_get(rpkg, PKG_ATTR_REPONAME, &rrepo);

			/* check if remote version is newer */
			if (pkg_version_cmp(ver, rver) < 0) {
				cnt++;
				if (!quiet) {
					printf("\t%s: %s -> %s [%s]\n",
					    name, ver, rver, rrepo);
				} else {
					break;
				}
			}
		}
		pkgdb_it_free(rit);
	}
	pkgdb_it_free(it);
	return (cnt);
}

void
usage_upgradable(void)
{
	fprintf(stderr, "Usage: pkg upgradable [-gqUx] [-r reponame] <pkg-name> ...\n");
	fprintf(stderr, "For more information see 'pkg help upgradable'.\n");
}

int
exec_upgradable(int argc, char **argv)
{
	struct pkgdb	*db = NULL;
	match_t		 match = MATCH_EXACT;
	c_charv_t	 reponames = vec_init();
	int ch, cnt = 0;

	struct option longopts[] = {
		{ "case-sensitive",	no_argument,		NULL,	'C' },
		{ "glob",		no_argument,		NULL,	'g' },
		{ "case-insensitive",	no_argument,		NULL,	'i' },
		{ "quiet",		no_argument,		NULL,	'q' },
		{ "repository",		required_argument,	NULL,	'r' },
		{ "no-repo-update",	no_argument,		NULL,	'U' },
		{ "regex",		no_argument,		NULL,	'x' },
		{ NULL,			0,			NULL,	0   },
	};

	while ((ch = getopt_long(argc, argv, "+gqr:Ux", longopts, NULL)) != -1) {
		switch (ch) {
		case 'C':
			pkgdb_set_case_sensitivity(true);
			break;
		case 'g':
			match = MATCH_GLOB;
			break;
		case 'i':
			pkgdb_set_case_sensitivity(false);
			break;
		case 'q':
			quiet = true;
			break;
		case 'r':
			vec_push(&reponames, optarg);
			break;
		case 'U':
			auto_update = false;
			break;
		case 'x':
			match = MATCH_REGEX;
			break;
		default:
			usage_upgradable();
			vec_free(&reponames);
			return (EXIT_FAILURE);
			/* NOTREACHED */
		}
	}
	argc -= optind;
	argv += optind;

	/* XXX current code never updates the repo */

	if (pkgdb_access2(PKGDB_MODE_READ,
	    PKGDB_DB_LOCAL | PKGDB_DB_REPO, &reponames) != EPKG_OK) {
		warnx("Failed to open local database");
		vec_free(&reponames);
		return (EXIT_FAILURE);
	}

	if (pkgdb_open_all2(&db, PKGDB_REMOTE, &reponames) != EPKG_OK) {
		warnx("Failed to open remote database");
		vec_free(&reponames);
		return (EXIT_FAILURE);
	}

	if (pkgdb_obtain_lock(db, PKGDB_LOCK_READONLY) != EPKG_OK) {
		pkgdb_close(db);
		warnx("Cannot get a read lock on the database. "
		      "It is locked by another process");
		return (EXIT_FAILURE);
	}

	if (argc == 0) {
		/* iterate over all installed packages */
		cnt = upgradable(db, NULL, MATCH_ALL, &reponames);
	} else {
		while (argc-- > 0) {
			/* iterate over installed packages that match this
			 * argument */
			cnt += upgradable(db, *argv, match, &reponames);
		}
	}

	pkgdb_release_lock(db, PKGDB_LOCK_READONLY);
	return (cnt > 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}
