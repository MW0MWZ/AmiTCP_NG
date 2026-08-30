#ifndef NG_HOSTNAME_H
#define NG_HOSTNAME_H
/*
 * AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
 *
 * The maximum length of this machine's host name -- ONE definition, shared by the
 * library and by the tools.
 *
 * WHY THIS FILE EXISTS. The value used to be written twice: MAXHOSTNAMELEN was 64
 * in netinclude/rpc/types.h and 255 in tools/CheckAmiTCPNGConfig.c, which each
 * carried its own #define. Nothing kept them equal and they drifted, so the config
 * checker happily passed a HOSTNAME= that the stack then truncated to 64 without
 * saying a word. Correcting one number would have left exactly the same trap for
 * whoever edited next; the duplication is the defect, so the duplication is what
 * is removed. Both builds already have -Isrc on the include path, so both sides can
 * reach this header without a build-flag change.
 *
 * WHY 255 AND NOT 64. 64 is the 4.3BSD value, from when the host name store held
 * the SHORT node name and the domain lived elsewhere. This stack deliberately
 * stores the FULLY QUALIFIED name -- gethostname() returns "amiga.example.org",
 * not "amiga", and the DHCP client joins host-name and domain-name into that store
 * on purpose. 64 was therefore enforcing a limit for a meaning we no longer use,
 * and a perfectly ordinary corporate FQDN can exceed it. 255 is the POSIX-mandated
 * FLOOR (_POSIX_HOST_NAME_MAX) -- the smallest limit a conforming system may
 * impose -- and it is what the checker had already assumed. Note it is NOT "what
 * everyone uses": glibc/Linux still sets HOST_NAME_MAX to 64, the classic BSD
 * value, because there the host name is the short node name and the domain is the
 * resolver's business. Here it is not, which is the whole reason for this file.
 *
 * NOT 253, deliberately. 253 is the true maximum length of a DNS name in
 * presentation format (255 wire octets less the leading length and root labels).
 * That is a rule about DNS SYNTAX, and this is a STORAGE bound; conflating them
 * would quietly turn a buffer size into a validation policy. If a 253-character
 * cap is wanted it belongs in ng_hostname_valid() as an explicit rule, argued on
 * its own merits -- not smuggled in as a buffer dimension.
 *
 * Anything sizing a buffer for a host name should use NG_MAXHOSTNAME + 1 (for the
 * NUL) rather than a literal. In particular the DHCP host-name/domain-name join
 * must be able to hold a full-length result: if the joined name does not fit it
 * fails validation and silently degrades to the unqualified name, which looks
 * exactly like the join never happening.
 */

#define NG_MAXHOSTNAME 255

#endif /* NG_HOSTNAME_H */
