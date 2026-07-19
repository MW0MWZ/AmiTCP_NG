/*
 * AmiTCP_NG -- a modernised, open fork of AmiTCP/IP 3.0b2.
 * Copyright (C) 2026 Andy Taylor (MW0MWZ).
 * Licensed under the GNU General Public License, version 2 (see COPYING).
 */

RCS_ID_C="amitcp_ng_glue.c";
/*
 * amitcp_ng_glue.c --- PORT (AmiTCP_NG) glue for the bebbo/clib2 build.
 *
 * Small shims for symbols the 1994 AmiTCP sources reference but which the
 * modern m68k-amigaos-gcc 6.5 + clib2 runtime does not provide. Kept in one
 * clearly-marked place so the port surface is auditable.
 */

/*
 * gethostname() here is the bsdsocket.library API form -- first argument is the
 * SocketBase -- called internally by the ARexx handler rexx_gethostname()
 * (kern/amiga_cstat.c). The real value comes from the library host id
 * (SBTC_HOSTID). Provide a safe stub so the stack links; it returns an empty
 * name successfully.
 * TODO: wire to the configured host id, and match the register-argument
 * calling convention of the other API entry points (this stub is stack-args).
 */
int
gethostname(void *SocketBase, char *name, int namelen)
{
  (void)SocketBase;
  if (namelen > 0)
    name[0] = '\0';
  return 0;
}
