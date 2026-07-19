# $Id: GNUmakefile,v 1.54 1994/05/12 15:23:42 jraja Exp $
#
# Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>,
#                    Helsinki University of Technology, Finland.
#                    All rights reserved.
#
# GNUmakefile for AmiTCP/IP Project
#
# Created      :     Nov 20 00:00:00 1992 Jarno Rajahalme
# Last modified: Tue May 10 01:18:42 1994 jraja
# 
# HISTORY
# $Log: GNUmakefile,v $
# Revision 1.54  1994/05/12  15:23:42  jraja
# Made bumprev path relative to allow usage on aplhas, too.
#
# Revision 1.53  1994/03/26  10:18:02  too
# Added kern/accesscontrol.[cho]
#
# Revision 1.52  1994/03/26  10:15:29  too
# Changes made by ppessi
#
# Revision 1.51  1994/02/15  20:47:27  jraja
# Removed SCOPTIONS from distribution (Smakefile generates it).
#
# Revision 1.50  1994/02/03  21:44:52  ppessi
# Added net/sana2config.[hc] to distribution,
# removed "net/if_dl.h", "net/sana2tags.c".
#
# Revision 1.49  1994/01/23  22:15:00  jraja
# Added net/sana2perror.c to the distribution, removed special rule for it.
#
# Revision 1.48  1994/01/12  07:13:36  jraja
# Moved sys/cdefs.h to the netinclude.
#
# Revision 1.47  1994/01/09  21:17:52  too
# Added amiga_errlists.c to API_C files
#
# Revision 1.46  1994/01/08  17:48:44  too
# Added api/amiga_generic2.c, api/amiga_sendrecv.c and api/sockargs.h
#
# Revision 1.45  1993/11/07  00:35:06  ppessi
# Added IP checksum in asm.
#
# Revision 1.44  1993/10/14  00:06:43  ppessi
# Changed directory name from src back to amitcp
#
# Revision 1.43  1993/08/10  18:57:07  ppessi
# Added revision files to the distribution.
#
# Revision 1.42  1993/08/09  21:27:16  ppessi
# Added revision bumping.
#
# Revision 1.41  1993/08/04  07:43:21  jraja
# Added kern/config_var.c to the distribution, so that it is not necessary
# to have gawk unless the kern/variables.src is modified.
#
# Revision 1.40  1993/08/04  07:39:47  jraja
# Removed sana headers from the distribution.
#
# Revision 1.39  1993/08/01  22:01:16  ppessi
# Fixed the special rule for sana2perror.o.
#
# Revision 1.38  1993/07/21  13:21:09  too
# Removed netinclude headers. Now this dorectory is src (formerly ami)
#
# Revision 1.37  1993/06/12  23:23:50  too
# Added api/allocdatabuffer.c
#
# Revision 1.36  1993/06/12  11:24:19  jraja
# Added special rule for the sana2perror.o.
#
# Revision 1.35  1993/06/12  10:17:49  jraja
# Removed net/sana2perror.c, since it was moved to the netlib.
#
# Revision 1.34  1993/06/04  11:16:15  jraja
# Fixes for first public release.
#
# Revision 1.33  1993/06/03  00:27:35  jraja
# Added apicalls*.h.
#
# Revision 1.32  1993/06/02  19:39:46  too
# fix
#
# Revision 1.31  1993/06/02  19:25:27  too
# Moved resolver stuff from kern/ to api/ -directory
#
# Revision 1.30  1993/06/01  16:55:03  too
# Added resolver sources
#
# Revision 1.29  1993/06/01  08:05:00  puhuri
# Removed ENVARC (not needeed anymore)
#
# Revision 1.28  1993/05/24  19:30:54  ppessi
# Moved sys/syslog.h into netinclude; sanitized prototypes
#
# Revision 1.27  1993/05/16  23:33:59  ppessi
# New RCS targets, alllocks, mylocks and verlocks.
# The sana2errlist.c moved to the sana2 support library.
#
#

BINDIR= /p/projects/amiga-gcc/bin
CC= $(BINDIR)/gcc-amigados
AS= $(BINDIR)/gas
LD= $(CC)
#LD= $(BINDIR)/ld
#LD= $(BINDIR)/ld-nostdlib

BUMPER= ../../../bin/bumprev

MAKE = gmake
MAKEFILE = GNUmakefile
GCCOPTIONS= GCCOPTS
CFLAGS= `egrep -v '^\#' $(GCCOPTIONS)`
CFLAGS_NO_OPT = `egrep -v '(^\#)|(-finline)|(-O)' $(GCCOPTIONS)` 

DIST_AMI = $(DIST)/amitcp

#
# System headers
#
SYS_H= \
	sys/synch.h sys/uio.h \
	sys/kernel.h sys/malloc.h sys/mbuf.h sys/queue.h \
	sys/socketvar.h sys/domain.h sys/protosw.h \
	sys/systm.h 

PROTOS_H= \
	protos/kern/amiga_api_protos.h 	protos/kern/amiga_main_protos.h \
	protos/kern/amiga_select_protos.h protos/kern/amiga_time_protos.h \
	protos/kern/amiga_userlib_protos.h protos/kern/kern_malloc_protos.h \
	protos/kern/kern_synch_protos.h protos/kern/subr_prf_protos.h \
	protos/kern/uipc_domain_protos.h \
	protos/kern/uipc_socket2_protos.h protos/kern/uipc_socket_protos.h \
	protos/net/if_loop_protos.h protos/net/if_protos.h \
	protos/net/radix_protos.h protos/net/raw_cb_protos.h \
	protos/net/raw_usrreq_protos.h protos/net/rtsock_protos.h \
	protos/netinet/in_cksum_protos.h protos/netinet/in_pcb_protos.h \
	protos/netinet/in_proto_protos.h protos/netinet/in_protos.h \
	protos/netinet/ip_icmp_protos.h protos/netinet/ip_input_protos.h \
	protos/netinet/ip_output_protos.h protos/netinet/raw_ip_protos.h \
	protos/netinet/tcp_debug_protos.h protos/netinet/tcp_input_protos.h \
	protos/netinet/tcp_output_protos.h protos/netinet/tcp_subr_protos.h \
	protos/netinet/tcp_timer_protos.h protos/netinet/tcp_usrreq_protos.h \
	protos/netinet/udp_usrreq_protos.h

# Protocol inspecific network routines:
# network interfaces, input queue scheduling, 
# raw sockets, routing
NET_C= \
	net/if.c net/if_loop.c \
	net/raw_cb.c net/raw_usrreq.c \
	net/route.c net/rtsock.c net/radix.c \
	net/if_sana.c net/netisr.c net/sana2config.c \
	net/sana2copybuff.c net/sana2arp.c net/sana2perror.c

NET_H= \
	net/if_types.h net/if_sana.h \
	net/sana2request.h net/sana2arp.h net/sana2config.h \
	net/netisr.h net/raw_cb.h net/radix.h 

# netinet -- internet protocols (ip, icmp, tcp, udp, arp)
NETINET_C= \
	netinet/in.c netinet/in_cksum.c netinet/in_pcb.c netinet/in_proto.c \
	netinet/ip_icmp.c \
	netinet/ip_input.c netinet/ip_output.c netinet/raw_ip.c \
	netinet/tcp_debug.c netinet/tcp_input.c netinet/tcp_output.c \
	netinet/tcp_subr.c netinet/tcp_timer.c netinet/tcp_usrreq.c \
	netinet/udp_usrreq.c

NETINET_H= \
	netinet/in_pcb.h netinet/in_var.h netinet/icmp_var.h  \
	netinet/tcpip.h netinet/tcp_debug.h netinet/tcp_fsm.h \
	netinet/tcp_seq.h netinet/tcp_timer.h netinet/tcp_var.h \
	netinet/udp_var.h 


# "Kernel" sources
# main, timeouts, syncronization, memory management,
# BSD socket ("Unix interprocess communication") routines,
# logging and netstat rexx interface 
KERN_C= \
	kern/amiga_main.c kern/amiga_time.c kern/amiga_config.c \
	kern/amiga_netdb.c \
	kern/kern_synch.c kern/kern_malloc.c kern/uipc_mbuf.c \
	kern/uipc_domain.c kern/uipc_socket.c kern/uipc_socket2.c \
	kern/amiga_log.c kern/amiga_cstat.c kern/amiga_rexx.c \
	kern/subr_prf.c kern/accesscontrol.c

KERN_H= \
	kern/amiga_includes.h kern/amiga_time.h kern/amiga_config.h \
	kern/amiga_netdb.h kern/amiga_log.h kern/amiga_rexx.h \
	kern/amiga_subr.h kern/accesscontrol.h

# Application interface sources
# Amiga "struct Library" interface, API functions,
API_C=\
	api/amiga_api.c api/amiga_libtables.c api/amiga_syscalls.c \
	api/amiga_sendrecv.c api/amiga_generic.c api/amiga_generic2.c \
	api/amiga_libcalls.c api/amiga_errlists.c \
	api/getxbyy.c api/gethostnamadr.c api/allocdatabuffer.c \
	api/res_comp.c api/res_debug.c api/res_init.c \
	api/res_mkquery.c api/res_query.c api/res_send.c  \
	api/auto_extras.c api/auto_inetaddr.c api/auto_netdb.c \
	api/auto_nonsocket.c api/auto_protocols.c api/auto_socket.c 
	#api/gethna_nores.c

API_H=\
	api/amiga_raf.h api/amiga_api.h api/amiga_libcallentry.h \
	api/allocdatabuffer.h api/gethtbynamadr.h \
	api/arpa_nameser.h api/resolv.h api/sockargs.h \
	api/apicalls.h api/apicalls_sasc.h api/apicalls_gnuc.h

#
# Misc headers
#
MISC_H=\
	all_includes.h conf/conf.h conf/rcs.h bsdsocket.library_rev.h
# 	clib/exec_protos.h 

#
# Misc sources
#
MISC= \
	all_includes.c GNUmakefile Smakefile GCCOPTS \
	kern/variables.src kern/config_var.awk kern/config_var.c \
	bsdsocket.library_rev.rev sys/mbuf.i netinet/in_cksum.asm

NET_O=      $(NET_C:.c=.o)
NETINET_O=  $(NETINET_C:.c=.o)
KERN_O=     $(KERN_C:.c=.o) kern/config_var.o
API_O=     $(API_C:.c=.o)
CSRCS=	$(API_C) $(KERN_C) $(NET_C) $(NETINET_C)
OBJS= 	$(API_O) $(KERN_O) $(NET_O) $(NETINET_O) 
AMISRC=      $(CSRCS) $(SYS_H) $(API_H) $(KERN_H) $(NET_H) $(NETINET_H) \
	     $(MISC_H) $(MISC) $(PROTOS_H)
#NETINCSRC=   $(SYS_HH) $(NET_HH) $(NETINET_HH) 
SRCS=	     $(AMISRC) #$(NETINCSRC)
LIBSRC= #-lamy
LIBS= #-nostdlib
TARGETS= amitcp
DIRS= net netinet kern api

all: dirs $(TARGETS)

opts: $(GCCOPTIONS)

$(GCCOPTIONS): # $(MAKEFILE)
	echo "$(COPTIONS)" >$@

$(TARGETS): $(OBJS)
	$(LD) -o $@ $(OBJS) $(LIBSRC) $(LIBS)

dirs: $(DIRS)

net: $(NET_O)
netinet: $(NETINET_O)
kern: $(KERN_O)
api: $(API_O)

clean:
	-rm -f net/*.o net/*.out net/*~
	-rm -f netinet/*.o netinet/*.out netinet/*~
	-rm -f kern/*.o kern/*.out kern/*~
	-rm -f api/*.o api/*.out api/*~
	-rm -f *.o *.out *~ core amitcp          # remove .o's too

depend:	
	echo "# DO NOT DELETE THIS LINE -- make depend depends on it." >depend
	makedepend -f depend -- $(CFLAGS) -- $(CSRCS)

.c.o: $(GCCOPTIONS)
	$(CC) $(CFLAGS) -c $*.c -o $*.o

.c.s: $(GCCOPTIONS)
	$(CC) $(CFLAGS) -S $*.c -o $*.s

# Special rule for variables
kern/config_var.c: kern/variables.src kern/config_var.awk
	-rm -f $@
	gawk -f kern/config_var.awk -v TARGETTI=C $< > $@ || rm $@

# Special rule for TeX
kern/config_var.tex: kern/variables.src kern/config_var.awk
	-rm -f $@
	gawk -f kern/config_var.awk -v TARGETTI=TEX $< > $@ || rm $@

# Special rules for files which broke optimization
kern/subr_prf.o: kern/subr_prf.c
	$(CC) $(CFLAGS_NO_OPT) -c kern/subr_prf.c -o $@

api/amiga_generic.o: api/amiga_generic.c
	$(CC) $(CFLAGS_NO_OPT) -c api/amiga_generic.c -o $@

# Create tags
TAGS: 
	etags $(SRCS)

# Archive for sources
amitcp.lha: $(SRCS) # locks
	lha a $@ $ $(SRCS)

# Make a distribution archive
DIST:   DIST_AMI

DIST_AMI: BUMPREV $(AMISRC)
	test -d $(DIST) || mkdir $(DIST) 
	test -d $(DIST_AMI) || mkdir $(DIST_AMI)
	tar cf - $(AMISRC) | (cd $(DIST_AMI); tar xf -)

RELEASE: $(AMISRC)
	for F in $(AMISRC) ; do \
	  rlog -R $$F && { \
	    if ident -q $$F | egrep -v '\$$Log' | fgrep '$$' >/dev/null ; \
	      then rcs -q '-N$(RELEASE):$$' $$F ; \
	      else rcs -q '-N$(RELEASE):' $$F ;\
	    fi ; \
          } ;\
	done ; \
	echo "RELEASE '$(RELEASE)' done" 

# Bump the revision
BUMPREV:
	$(BUMPER) 3 bsdsocket.library_rev

# Test for RCS locks
locks: 
	@egrep '^locks\
	[a-z]+' \
	`echo $(CSRCS) $(SYS_H) $(API_H) $(KERN_H) $(NET_H) $(NETINET_H) $(MISC) \
	| tr -cs "[!-~]" "[\012*]" | sed 's-[^/]*$$-RCS/&,v-g' `

alllocks:
	@rlog -R -L $(CSRCS) \
		$(SYS_H) $(API_H) $(KERN_H) \
		$(NET_H) $(NETINET_H) $(MISC)

mylocks:
	@rlog -R -l`whoami` -L $(CSRCS) \
		$(SYS_H) $(API_H) $(KERN_H) \
		$(NET_H) $(NETINET_H) $(MISC)

verlocks:
	@rlog -h -L $(CSRCS) \
		$(SYS_H) $(API_H) $(KERN_H) \
		$(NET_H) $(NETINET_H) $(MISC)

include depend
