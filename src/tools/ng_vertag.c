/*
 * AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
 *
 * A single $VER: version cookie, linked into every AmiTCP_NG command-line tool so
 * `version <tool>` reports the AmiTCP_NG project version. Kept in its own object
 * (not a header) so it lands in every tool regardless of which LVO wrappers the tool
 * uses -- some tools include ng_lvo.h, some roll their own inline calls. The version
 * is single-sourced from src/bsdsocket.library_rev.h (built with -Isrc).
 */
#include <exec/types.h>
#include <bsdsocket.library_rev.h>	/* AMITCP_NG_VER / DATE */

const char ng_tool_vertag[] = "$VER: AmiTCP_NG " AMITCP_NG_VER " (" DATE ")";
