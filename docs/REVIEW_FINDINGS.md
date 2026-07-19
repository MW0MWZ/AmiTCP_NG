# AmiTCP_NG — code review findings (correctness + security)

Consolidated from a focused correctness-and-security review across the whole
codebase (2026-07-16). Each finding lists severity, location, the defect, a fix,
and a **class**: `INHERITED` (present in the 1994 4.4BSD/AmiTCP base, not a port
regression), `AMITCP` (AmiTCP-specific design), or `PORT` (introduced by the
AmiTCP_NG modern-gcc port). Status starts `[ ]` (pending); becomes `[x]` when
fixed + compile-tested, or `[HOLD]` for protocol-hardening items awaiting a
policy decision.

**Target context that raises severity:** the 68000 has NO MMU, and mbufs are a
single GLOBAL pool shared by every socket and the drivers — so a bad pointer or
pool-exhaustion bug corrupts/DoSes the *whole machine*, and network-triggerable
ones are remotely so.

**Areas reviewed:** config/resolver, routing+glue, TCP, IP/ICMP/UDP, SANA-II, the
mbuf memory core (`uipc_mbuf.c`, A29–A31) and the socket layer / API
(`uipc_socket.c`, `ObtainSocket`/`CloseSocket`, A32–A35).

---

## A. Memory-safety / overflow — FIX UNCONDITIONALLY

These are clear correctness/memory-safety defects; fixing them is not a behaviour
change anyone should object to. Most-severe first.

### A1. `[x]` CRITICAL — gethostbyname address array overflow (remote, hostile DNS reply). INHERITED
`api/gethostnamadr.c` `getanswer()`. The T_A path writes into `h_addr_ptrs[MAXADDRS]`
(35 slots) with no count guard (the alias path *does* guard). A DNS reply packed
with many compressed A records overflows the pointer array into the adjacent
hoststruct/hostbuf. **Fix:** add `if (HS->h_addr_count >= MAXADDRS) continue;`
before `*hap++ = bp`.

### A2. `[x]` CRITICAL — gethostbyname numeric-name strcpy overflow. INHERITED
`api/gethostnamadr.c` `_gethostbyname()`. For a digit-leading name, a 28-byte heap
block is `strcpy`'d into, but the lenient `inet_aton` (`amiga_libcalls.c`) accepts
arbitrarily long numeric strings → heap overflow. **Fix:** reject/size-check
`strlen(name) > 15` before this branch (or size the alloc from strlen).

### A3. `[x]` CRITICAL — read_icmphist unbounded write into 255-byte stack ARexx buffer. AMITCP
`kern/amiga_cstat.c` `read_icmphist()`. Bypasses the `cs_putchar` bounds discipline
every other writer uses; writes ~418 bytes into `rbuf[REPLYBUFLEN]` (255, on the
stack in `amiga_rexx.c`). **Fix:** rewrite with `csprintf(res, "%lu ", ...)`.

### A4. `[x]` CRITICAL — routing `rtioctl` copies unclamped `sa_len` into the routing table (LIVE). INHERITED
`net/route.c` `rtioctl()`→`rtrequest()`. `SIOCADDRT` with a large `rt_gateway.sa_len`
`Bcopy`s past the fixed 16-byte ortentry field into the persistent route (info
disclosure); unprivileged (suser is compiled out under AMITCP). **Fix:** clamp both
`sa_len` to `sizeof(struct sockaddr)` in `rtioctl()` (reject if larger).

### A5. `[x]` CRITICAL — `socket(PF_ROUTE)` jumps through NULL vector → crash (LIVE). AMITCP
`net/rtsock.c` (`route_usrreq`/`route_output` are `#define ...NULL` under AMITCP) +
`kern/uipc_socket.c` `socreate()` calls `pr_usrreq` with no NULL check. Any process
opening a PF_ROUTE socket crashes the machine. **Fix:** give the routesw entry real
`EOPNOTSUPP` stubs (preferred), AND add a NULL-`pr_usrreq` guard in `socreate()`.
NOTE: implementing route_output/usrreq for real (Roadshow shim) requires A6+B-parser
fixes first.

### A6. `[HOLD-until-A5-real-impl]` CRITICAL — `rn_addmask` heap overflow from unclamped mask `sa_len`. INHERITED
`net/radix.c` `rn_addmask()`. `mlen = *(u_char*)netmask` copied into a `MAXKEYLEN`(32)
buffer with no bound. Currently unreachable (blocked by A5's NULL), becomes live the
moment route_output is implemented. **Fix now (defensive):** `if (mlen==0 || mlen>MAXKEYLEN) return 0;`.

### A7. `[x]` HIGH — TCP `ti_len` signed-16-bit sign-flip bypasses receive-window enforcement. INHERITED
`netinet/tcp_input.c` ~line 360. A reassembled segment >32767 bytes makes signed
`ti_len` negative → window trim skipped, oversized mbuf delivered to the socket
buffer; bookkeeping corrupts. **Fix:** compute length in `u_int`, reject/trim
`tlen > 32767` before storing to `ti_len`; treat `ti_len` as unsigned throughout.

### A8. `[x]` HIGH — IP reassembly length not bounded to 65535 (ping-of-death). INHERITED
`netinet/ip_input.c` `ip_reass()`. Reassembled total isn't checked against
`IP_MAXPACKET` before storing into signed 16-bit `ip_len`; wraps → downstream uses
truncated length while a large tail rides along. **Fix:** `if (next > IP_MAXPACKET)
{ ips_toolong++; ip_freef(fp); goto dropfrag; }`.

### A9. `[x]` HIGH — DNS RDLENGTH used as bcopy length without checking vs message end (info leak). INHERITED
`api/gethostnamadr.c` `getanswer()`. `n`(=RDLENGTH) checked only vs output buffer,
not `eom - cp` → over-read past the packet into the returned hostent. **Fix:**
`if (n > eom - cp) break;` before the bcopy. (Also clamp `h_length` to 4 for C_IN.)

### A10. `[x]` HIGH — SANA-II `if_addrlen` never clamped → stack smash on every packet. AMITCP
`net/if_sana.c` `iface_make()` line ~439. `if_addrlen` from `S2_DEVICEQUERY` is used
unbounded in dozens of `bcopy` into fixed 16-byte (`MAXADDRSANA`) buffers, incl. the
stack `hwaddr[]` in `sana_arp_read()`. A driver misreporting AddrFieldSize corrupts
on every frame. **Fix:** after computing it, `if (if_addrlen > MAXADDRSANA) goto fail;`.

### A11. `[x]` HIGH — SANA-II mbuf-fill bounds check is gated by `DIAGNOSTIC`. AMITCP
`net/sana2copybuff.c` `m_copy_to_mbuf`/`m_copy_from_mbuf`. The `if (m == 0)` short-chain
guard — the ONLY thing between a driver over-length copy and a NULL deref writing wire
data to page zero (Exec vectors on a no-MMU 68000) — is `#if DIAGNOSTIC`. **Fix:** make
that check unconditional; keep only the extra logging under DIAGNOSTIC.

### A12. `[x]` HIGH — config `setvalue` "Done." append bypasses CSource bounds. AMITCP
`kern/amiga_config.c` `setvalue()`. Raw `bcopy(DONE,...)` + NUL past `CS_Length` after a
VAR_FUNC handler filled the buffer → overruns the 255-byte stack ARexx reply. **Fix:**
guard against `CS_Length` or route through `csprintf`.

### A13. `[x]` HIGH — `res_querydomain` OOB read / wild bcopy on empty name. INHERITED
`api/res_query.c`. `n = strlen(name)-1` then `name[n]`; empty name → `name[-1]`, and
if it's `.`, `bcopy(name,nbuf,-1)` (~4GB). **Fix:** reject `name[0]=='\0'` up front.

### A14. `[x]` MEDIUM-HIGH — `icmp_error` over-reads a truncated forwarded-packet copy (info leak). INHERITED
`netinet/ip_icmp.c` `icmp_error()`. Copies `oiplen+8` (up to 68) bytes from a 64-byte
`mcopy` (from `ip_forward`) → leaks stale mbuf memory into the outgoing ICMP error.
Only reachable if `ipforwarding` is on (see Q). **Fix:** `icmplen = min(icmplen, n->m_len)`.

### A15. `[x]` MEDIUM-HIGH — `gethostname()` signed-length underflow → memcpy(~4GB). INHERITED
`api/gethostnamadr.c`. `namelen==0` → `else namelen--` = -1 → `memcpy` count ~4GB.
**Fix:** reject `namelen <= 0` up front.

### A16. `[x]` MEDIUM — TCP options loop reads past option bytes. INHERITED
`netinet/tcp_input.c` `tcp_dooptions()`. Loop guards only `cnt>0`, reads `cp[1]`/`bcopy
cp+2` without `cnt>=optlen`. Currently within the mbuf's 100-byte array (stale-data
leak, UB), OOB if mbuf sizing changes. **Fix:** guard `cnt>1 && cnt>=optlen`.

### A17. `[x]` MEDIUM — `sethostname`/`rexx_sethostname` copies full buffer → stale-stack leak. AMITCP
`kern/amiga_cstat.c` `rexx_sethostname()` passes `sizeof(Buffer)` not `strlen`; every
later `gethostname` leaks stale stack. **Fix:** `sethostname(Buffer, strlen(Buffer))`.

### A18. `[x]` MEDIUM — `dn_expand` two latent 1-byte OOB reads (no self-guard). INHERITED
`api/res_comp.c`. Reads first label byte / pointer low byte before `cp < eomorig`.
Masked by the current caller, but the function should self-guard. **Fix:** add
`if (cp >= eomorig) return -1;` at loop entry and before the pointer read.
(Positive: the compression-pointer loop-limit guard IS present — no infinite loop.)

### A19. `[x]` MEDIUM — signed `ip_off`/`ip_len` sign-extend corrupts fragment overlap math for >32KB datagrams. INHERITED
`netinet/ip_input.c` `ip_reass()`. Do the offset/length comparisons via `(u_short)`
casts. Compounds A8.

### A20. `[x]` MEDIUM — netdb access-mask shift count unbounded (undefined shift). AMITCP
`kern/amiga_netdb.c` `addaccessent()`. `8*dots` can exceed 31 → UB, mis-derived
ACCESS-control mask. **Fix:** cap `dots` at 3.

### A21. `[x]` MEDIUM — `sana2perror` off-by-one array read. AMITCP
`net/sana2perror.c` line ~48. `-err > io_nerr` should be `>=` (reads `io_errlist[io_nerr]`,
one past end, deref as string). **Fix:** `>=`.

### A22. `[x]` MEDIUM — `ssconfig_parse` NULL deref on AllocVec failure. AMITCP
`net/sana2config.c`. `config->rdargs` read before the `if (config != NULL)`. **Fix:**
`if (config == NULL) return NULL;` first.

### A23. `[x]` MEDIUM — TCP urgent-pointer guard is a no-op; `tcp_pulloutofband` panic reachable. INHERITED
`netinet/tcp_input.c`. `ti_urp + sb_cc > SB_MAX` can't fire (ti_urp max 65535 < SB_MAX);
`so_oobmark` set past real data; `panic()` = remote machine halt. **Fix:** bound urgent
pointer against `rcv_nxt+rcv_wnd`, audit the panic path.

### A24. `[x]` LOW — uninitialised `dest` passed to `icmp_error` from `ip_dooptions`. INHERITED
`netinet/ip_input.c`. Harmless today (only read for REDIRECT), UB. **Fix:** `= {0}`.

### A25. `[x]` LOW — `in_cksum` unconditional `printf` on truncated len (remote log flood). INHERITED
`netinet/in_cksum.c`. **Fix:** gate behind DIAGNOSTIC/ipprintfs. (Positive: in_cksum
is otherwise OOB-safe by construction — stops at last mbuf regardless of len.)

### A26. `[x]` LOW — `res_querydomain` nbuf may be non-NUL-terminated → dn_comp reads adjacent heap onto wire. INHERITED
`api/res_query.c`/`res_comp.c`. **Fix:** NUL-terminate nbuf; reject names >= MAXDNAME.

### A27. `[x]` LOW — `icmp_reflect` option copy relies on mbuf sizing, no bound check. INHERITED
`netinet/ip_icmp.c`. Safe today (44 < MHLEN 100) but undocumented. **Fix:** add a bound.

### A28. `[x]` LOW — `raw_ctlinput` off-by-one (`cmd > PRC_NCMDS`). INHERITED
`net/raw_usrreq.c`. Inert (body incomplete) but wrong; fix before it's used: `>=`.

### A29. `[x]` CRITICAL — mbuf-pool config integer overflow → heap smash. AMITCP
`kern/uipc_mbuf.c`. `mb_check_conf()` enforced only lower bounds on the config-file
MBUF tunables, so a large value overflowed `MSIZE*(howmany+1)` / the cluster stride,
wrapped the AllocMem size small, and the fill loops wrote past it. **Fixed:** upper
bounds in `mb_check_conf()` + independent overflow guards in `m_alloc()`/`m_clalloc()`.

### A30. `[x]` HIGH — `tsleep` timer deadlock (PA_IGNORE reply port). AMITCP
`kern/kern_synch.c` `tsleep_send_timeout()`. A prior `tsleep_abort_timeout()` leaves the
timer's reply port in PA_IGNORE; the AbortIO()+WaitIO() to reclaim the request then
`Wait()`s forever because a PA_IGNORE reply raises no signal → hung socket task.
**Fixed:** re-arm the port to PA_SIGNAL before WaitIO().

### A31. `[x]` MEDIUM — `mb_read_stats` unbounded sprintf. AMITCP
`kern/uipc_mbuf.c`. Wrote MTCOUNT+1 numbers with raw `sprintf` ignoring `CS_Length`.
**Fixed:** routed through the bounded `csprintf`/`cs_putchar`.

### A32. `[x]` CRITICAL — systemic unchecked `M_WAIT` allocations (NULL deref). AMITCP
This port's fixed mbuf/socket pool does NOT block for `M_WAIT`, so `MGET/MGETHDR/m_get/
MALLOC` can return NULL where stock BSD guaranteed success. **Fixed** at every offending
site: `socreate` (uipc_socket.c), `sosend`, `soreceive` (OOB), `sogetopt`, `_accept`
(amiga_syscalls.c), `rtinit` (route.c), `ip_ctloutput` (×2), `rip_ctloutput`,
`tcp_ctloutput`. (Sites that were already guarded — `setsockopt`, `sockargs` — verified.)

### A33. `[x]` HIGH — `_ObtainSocket`/`ObtainSocket` NULL `prp` deref. AMITCP
`api/amiga_generic.c`. `prp->pr_type` read before the `prp == 0` check. **Fixed.**

### A34. `[x]` HIGH — `_CloseSocket` out-of-range fd → OOB `FD_CLR`. AMITCP
`api/amiga_generic.c`. **Fixed:** `(unsigned)fd >= dTableSize` bounds check first.

### A35. `[x]` MEDIUM — `ObtainSocket` domain match too strict / wrong `so_proto` compare. AMITCP
`api/amiga_generic.c`. **Fixed:** compare `so_proto` only when a domain was requested.

---

## B. Protocol hardening — REVIEWED, DECISIONS APPLIED

These change protocol behaviour vs. the 1994 baseline. All six were re-reviewed against
the actual code; the maintainer chose the full recommended set. Every implemented item
was compile-tested and the UDP loopback round-trip re-test passes with them in.

- `[x]` B1. **IMPLEMENTED.** Unbounded IP fragment reassembly queue → global mbuf-pool
  DoS. Confirmed: `ip_reass` had no cap, only the ~30s timeout. Added `IP_MAXFRAGPACKETS`
  (8) + `ip_nfrags` counter; evict oldest (`ipq.prev`) when full; counter kept in sync in
  `ip_freef`. (`netinet/ip_input.c`.) INHERITED.
- `[x]` B2. **IMPLEMENTED.** Unbounded TCP reassembly queue → global mbuf-pool DoS.
  Confirmed: out-of-order queue not counted against `sb_hiwat`. Cap queued OOO bytes at
  `so_rcv.sb_hiwat`, drop excess (peer retransmits). Checked early, before any queue
  mutation. (`netinet/tcp_input.c` `tcp_reass`.) INHERITED.
- `[SKIP]` B3. No SYN-flood limit. **Already mitigated** — `sonewconn()` enforces
  backlog admission (`3*so_qlimit/2`, `SOMAXCONN`=5) and there is a 75s embryonic timeout;
  B2 bounds the memory. Worst case ~7 embryonic sockets/listener — not a real vector.
  Not worth SYN-cookie complexity (no entropy/CSPRNG on this target). INHERITED.
- `[x]` B4. **IMPLEMENTED (both halves).** Predictable DNS id + no reply-source check.
  Confirmed *worse* than filed: `res_send.c` used `recv()` (never `recvfrom`), so with ≥2
  nameservers (common) there was ZERO source filtering. Fixed: `recvfrom` + verify reply
  source addr/port vs the queried server. Also stirred the query id with `GetSysTime`
  microsecond jitter + counter (honestly documented as weak, not cryptographic).
  (`api/res_send.c`, `api/res_mkquery.c`.) INHERITED.
- `[DEFER]` B5. Blind in-window RST/SYN acceptance (Watson attack). Confirmed present
  (stock 4.4BSD), but full RFC-5961 challenge-ACK is the most state-machine-invasive change
  of the six, in the hottest function, on a no-MMU target — highest regression risk, poor
  threat-model fit for a WB3.2 host. Minimal future option: exact `seq==rcv_nxt` for RST
  only (~1 line at the `TH_RST` block). (`netinet/tcp_input.c`.) INHERITED.
- `[x]` B6. **IMPLEMENTED.** Inert ARP anti-spoof check. Confirmed dead (`#ifndef AMITCP`,
  and referenced nonexistent Ethernet globals `ac`/`etherbroadcastaddr` — wouldn't compile
  if enabled). Rewritten live against `if_addrlen`/`MAXADDRSANA`: reject an ARP packet whose
  claimed sender hw-address is all-broadcast. (`net/sana2arp.c` `in_arpinput`.) AMITCP.
- (PAWS/timestamps — **IMPLEMENTED** as full RFC 1323 timestamps, CPU-gated
  on for 68020+ and off for a bare 68000/68010. See *RFC 1323 timestamps* in
  [docs/BUILDING.md](BUILDING.md).)

---

## C. Port-specific / functional (not security)

- `[ ]` C1. `gethostname` glue is a STUB returning empty; ARexx HOSTNAME always empty.
  Delegate to the real `host_name`/`host_namelen` logic. (`src/amitcp_ng_glue.c`,
  `kern/amiga_cstat.c`.) PORT.
- `[ ]` C2. `gethostname` glue has no shared prototype between def and call → silent
  ABI drift risk under gnu89. Declare it in a shared header. PORT.
- `[note]` The `rtsock` protosw `(void(*)())` cast PORT change was VERIFIED a no-op
  (STKARGFUN is empty on this toolchain). No regression.
- `[note]` `#ifndef AMITCP` blocks in `if.c`/`sana2arp.c` reference undeclared
  identifiers; they compile out because we DO pass `-DAMITCP` (confirmed in ccflags.sh).
  Don't ever build without it (see B6 for the security consequence of them being dead).

---

## Open questions for the maintainer
1. **Hardening scope (B):** fix all of A unconditionally (recommended); which of B do
   you want? (My strong recommendation: take B1 and B2 regardless — trivial machine-
   wide DoS — and defer B3/B4/B5 as a "security-hardening" milestone.)
2. **Router or host?** Is `ipforwarding` ever enabled? If never, A14 and the forwarding
   leaks are unreachable and drop in priority.
3. **DNS resolver:** bug-for-bug BSD compat, or modern-hardened (affects B4)?
