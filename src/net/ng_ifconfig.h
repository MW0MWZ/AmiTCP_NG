#ifndef NG_IFCONFIG_H
#define NG_IFCONFIG_H
/*
 * AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
 *
 * ONE parser for an interface configuration file, shared by the tool that reads it
 * and by the stack that now reads it too.
 *
 * WHY THIS IS SHARED RATHER THAN COPIED. An interface can come back up two ways.
 * The operator runs Online, which switches the DEVICE online and nothing more --
 * Online/Offline talk to the driver with S2_ONLINE/S2_OFFLINE and never open
 * bsdsocket.library at all. Or the device itself reports S2EVENT_ONLINE because a
 * cable went back in or a radio reassociated. BOTH arrive at the stack the same
 * way, through sana_poll(), and in both cases no tool is running and nobody has
 * read the config file.
 *
 * The stack used to answer that by keeping a small snapshot of what it had been
 * told -- address, mask, default gateway -- and putting those three back. Anything
 * else the file asked for (extra routes, static name servers, the search domain)
 * was never recorded and so never came back: the interface returned half
 * configured, and quietly. The fix is for the stack to read the file, which means
 * the file's syntax now has two readers, and two readers of one format drift.
 * So there is one implementation and both link it.
 *
 * The parser deliberately IGNORES unknown keywords, exactly as Roadshow does, so a
 * newer config still works on an older stack. That forgiveness is also why
 * CheckAmiTCPNGConfig exists: a misspelled keyword configures nothing and says
 * nothing. ADD ANY NEW KEYWORD TO src/tools/ng_ifkeys.h as well, or the checker
 * will report it as a typo.
 */

#define NG_IFCFG_MAXNS	6
#define NG_IFCFG_VALLEN	64
#define NG_IFCFG_LINELEN 256

struct ng_ifcfg {
  char device[NG_IFCFG_VALLEN];
  long unit;
  int  dhcp;			/* configure=dhcp */
  int  have_address;
  char address[NG_IFCFG_VALLEN];
  char netmask[NG_IFCFG_VALLEN];
  char gateway[NG_IFCFG_VALLEN];
  char domain[NG_IFCFG_VALLEN];
  char ns[NG_IFCFG_MAXNS][NG_IFCFG_VALLEN];
  int  nns;
  int  initdelay;
  /*
   * CREATION-TIME settings. These size the SANA-II request rings and the socket
   * buffers when the interface is CREATED, and only AddInterfaceTagList consumes
   * them -- ConfigureInterfaceTagList ignores them by design.
   *
   * They matter to the tool and not to the stack's reconfigure path, and that is
   * not an oversight: going offline never scrubs them. They live on the softc,
   * which survives the device going away and coming back, so there is nothing to
   * put back and no reason for the stack to recreate an interface to apply them.
   */
  long ipreq;			/* iprequests=    (0 = the stack's RAM-tiered default) */
  long wreq;			/* writerequests= (0 = the stack's RAM-tiered default) */
  long mtu;			/* mtu=           (0 = the device's reported MTU)      */
  long sendspace;		/* tcp.sendspace= (0 = the stack's RAM-tiered default) */
  long recvspace;		/* tcp.recvspace= (0 = the stack's RAM-tiered default) */
  long mssdflt;			/* tcp.mssdflt=   (0 = auto: interface MTU - 40)       */
  long bps;			/* bps=           (0 = the driver's reported BPS)      */
};

/* Parse one keyword=value (or "keyword value") line into cfg. */
void ng_ifcfg_parse_line(char *line, struct ng_ifcfg *cfg);

/* Clear every field. Call before parsing into a reused struct. */
void ng_ifcfg_clear(struct ng_ifcfg *cfg);

/*
 * Read and parse one config file by PATH. Returns 1 if it parsed and named a
 * device, 0 otherwise.
 */
int ng_ifcfg_read_path(const char *path, struct ng_ifcfg *cfg);

/*
 * Find an interface's config by NAME and parse it: DEVS:NetInterfaces/<name>
 * first, then SYS:Storage/NetInterfaces/<name> -- the same two places, in the same
 * order, that AddNetInterface and Online/Offline already search. Returns 1 on
 * success.
 *
 * CALLS DOS. Task level only, and never from the network task: reading a file
 * blocks, and the network task is what moves the packets. The stack's own caller
 * hands this to a helper process for exactly that reason.
 */
int ng_ifcfg_find(const char *ifname, struct ng_ifcfg *cfg);

#endif /* NG_IFCONFIG_H */
