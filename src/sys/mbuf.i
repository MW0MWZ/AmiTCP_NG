	IFND	SYS_MBUF_I
SYS_MBUF_I	SET	1
;;; $Id: mbuf.i,v 1.1 1993/11/07 00:32:26 ppessi Exp $
;;;
;;; mbuf.i
;;;
;;; Author: ppessi <Pekka.Pessi@hut.fi>
;;;
;;; Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>,
;;;                    Helsinki University of Technology, Finland.
;;;
;;; Created      : Wed Oct 20 09:04:20 1993 ppessi
;;; Last modified: Wed Oct 20 10:59:35 1993 ppessi
;;;
;;; $Log: mbuf.i,v $
;; Revision 1.1  1993/11/07  00:32:26  ppessi
;; Initial revision
;;
;;;

	INCLUDE   "exec/types.i"

 STRUCTURE MBUF,0
    APTR    M_NEXT
    APTR    M_NEXTPKT
    LONG    M_LEN
    APTR    M_DATA
    WORD    M_TYPE 
    WORD    M_FLAGS
    LABEL   M_SIZE
    
	ENDIF
    
