/* AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
 *
 * Version of LIBS:usergroup.library.
 *
 * Major version 4, matching AmiTCP's own usergroup.library 4.x and Roadshow's
 * 4.31: the vector table is identical in all three (39 functions from bias 30,
 * verified against both the AmiTCP FD and the Roadshow SFD), so software that
 * does OpenLibrary("usergroup.library", 4) gets what it expects from any of
 * them. The revision and the "(AmiTCP_NG ...)" tag are what distinguish ours.
 *
 * UG_VERSION is the ABI and does NOT move with the project release number --
 * same rule as bsdsocket.library_rev.h.
 */
#ifndef USERGROUP_LIBRARY_REV_H
#define USERGROUP_LIBRARY_REV_H

#define UG_VERSION	4
#define UG_REVISION	1

#endif /* !USERGROUP_LIBRARY_REV_H */
