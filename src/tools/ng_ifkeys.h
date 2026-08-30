#ifndef NG_IFKEYS_H
#define NG_IFKEYS_H
/*
 * AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
 *
 * The keywords a DEVS:NetInterfaces/<name> file may contain.
 *
 * AddNetInterface.c is what actually parses these, and it deliberately IGNORES a
 * keyword it does not know so that a file written for a newer version still works.
 * That forgiveness is why CheckAmiTCPNGConfig has to carry the list: a typo like
 * "adress=" is silently ignored at boot and the interface simply comes up with no
 * address, which is a miserable thing to debug. The checker can say so.
 *
 * ADD A KEYWORD IN BOTH PLACES -- here, and in AddNetInterface.c's parse_line().
 * They are separate because the parser needs per-keyword code and the checker only
 * needs the names; keeping the names in one file is what stops them drifting.
 *
 * There are TWO lists: NG_IFKEYS is the public, documented set, and
 * NG_IFKEYS_UNADVERTISED below is accepted-but-not-advertised. A checker must
 * validate against both (or it will call a legitimate setting a typo); anything
 * that PRESENTS the keywords to a user should use NG_IFKEYS only.
 */

#define NG_IFKEYS(X)		\
  X("device")			\
  X("unit")			\
  X("configure")		\
  X("address")			\
  X("netmask")			\
  X("gateway")			\
  X("domain")			\
  X("nameserver")		\
  X("requiresinitdelay")	\
  X("iprequests")		\
  X("writerequests")		\
  X("mtu")			\
  X("tcp.sendspace")		\
  X("tcp.recvspace")		\
  X("tcp.mssdflt")

/*
 * Keywords that are ACCEPTED but deliberately NOT ADVERTISED.
 *
 * `bps=` overrides the link speed the SANA-II driver reports (S2_DEVICEQUERY
 * BPS), which drives the TCP window auto-tune and the SANA-II ring sizing. It
 * exists for the test harness -- an emulated NIC can only ever report its own
 * 10 Mbit, so without this there is no way to exercise the ~100 Mbit path that
 * real hardware takes. It is NOT a knob users should be reaching for: telling
 * the stack a lie about the link only makes it tune for a link that isn't there.
 *
 * So it must still be honoured, and must NOT be reported as a typo when someone
 * has genuinely set it -- but anything that ENUMERATES the supported keywords
 * (help text, documentation, a settings listing) should use NG_IFKEYS alone and
 * leave this out. Validation uses both; presentation uses only the public list.
 */
#define NG_IFKEYS_UNADVERTISED(X)	\
  X("bps")

/*
 * Values `configure=` may take. ONLY "dhcp" does anything in this build: the
 * parser tests for it literally, so anything else leaves the interface static.
 * Roadshow's own example files document "auto" and "fastauto" (RFC 3927
 * link-local), and copying one of those across gives an interface with no
 * address and no error -- so the checker names them specifically rather than
 * lumping them in with a typo.
 *
 * Link-local IS implemented, but as the fallback when DHCP finds no server
 * (ng_linklocal_acquire in api/amiga_roadshow_compat.c), not as a configure=
 * mode of its own.
 */
#define NG_IFCONFIGURE_SUPPORTED	"dhcp"

#endif /* !NG_IFKEYS_H */
