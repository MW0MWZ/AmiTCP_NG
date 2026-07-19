/* Prototypes for functions defined in
udp_usrreq.c
 */

/* Forward-declare the aggregate type named below, so the prototypes use the real
 * tag instead of a throwaway function-prototype-scope tag. Zero behaviour/ABI
 * impact. */
struct inpcb;

void udp_init(void);

void STKARGFUN udp_input(register struct mbuf * m,
			 int iphlen);

struct mbuf * udp_saveopt(caddr_t p,
                          register int size,
                          int type);

void udp_notify(register struct inpcb * inp,
		int errno);

void udp_ctlinput(int cmd,
                 struct sockaddr * sa,
                 register struct ip * ip);

int udp_output(register struct inpcb * inp,
               register struct mbuf * m,
               struct mbuf * addr,
               struct mbuf * control);

int udp_usrreq(struct socket * so,
               int req,
               struct mbuf * m,
               struct mbuf * addr,
               struct mbuf * control);

void udp_detach(struct inpcb * inp);


