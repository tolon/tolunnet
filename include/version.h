/*
 * tolunnet — single-source version string (CLOSE §D.14).
 * The Makefile extracts this for the package names; TolunnetControl and
 * the library SocketVersionInfo include it directly.
 */
#ifndef TOLUNNET_VERSION_H
#define TOLUNNET_VERSION_H

#define TOLUNNET_VERSION "1.2.0-rc5"

/* Release date of TOLUNNET_VERSION - the CHANGELOG release date, not
 * hand-edited per build (11aa item 1). ci/check_release_consistency.py
 * verifies it matches the top CHANGELOG section. */
#define TOLUNNET_VER_DATE "30.09.2026"

/* One shared $VER tag (11aa item 1): every shipped binary embeds
 * TN_VERSTAG("its-own-name") in a __attribute__((used)) string so
 * `Version C:<name>` and Workbench Information show the version. */
#define TN_VERSTAG(name) "$VER: " name " " TOLUNNET_VERSION " (" TOLUNNET_VER_DATE ")"

#endif /* TOLUNNET_VERSION_H */
