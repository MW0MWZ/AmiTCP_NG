/*
 * PORT (AmiTCP_NG): the library advertises major version 4. AmiTCP 3.0b2 shipped
 * bsdsocket.library 3.30, but AmiTCP_NG implements the Roadshow (v4) extension API
 * at its exact SFD vector offsets, so it presents itself as a version-4 library.
 * This is also required for Roadshow's own configuration tools to open it: they do
 * OpenLibrary("bsdsocket.library", 4) and would reject a version-3 library. The
 * revision (1) and the "(AmiTCP_NG)" tag keep it clearly distinct from Roadshow's
 * own 4.x releases.
 */
#define VERSION         4
#define REVISION        1
/*
 * AMITCP_NG_VER is the AmiTCP_NG PROJECT version (Major.Minor.Revision) -- the
 * release/patch counter. It is SEPARATE from the bsdsocket.library ABI version
 * (VERSION.REVISION = 4.1 above), which is what OpenLibrary("bsdsocket.library",4)
 * checks and Roadshow's tools rely on: that stays frozen for the whole 4.1.x line
 * and is NOT bumped for a patch. Bump AMITCP_NG_VER (and DATE) here for each
 * release -- this is the single source of truth (build-release.sh reads it too).
 */
#define AMITCP_NG_VER   "4.1.0"
#define DATE    "21.7.2026"
#define VERS    "bsdsocket.library 4.1"
#define VSTRING "bsdsocket.library 4.1 (AmiTCP_NG " AMITCP_NG_VER ")\n\r"
#define VERSTAG "\0$VER: bsdsocket.library 4.1 (AmiTCP_NG " AMITCP_NG_VER ")"
