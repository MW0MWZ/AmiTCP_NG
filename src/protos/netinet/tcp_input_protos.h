/* Prototypes for functions defined in
tcp_input.c
 */

/* Forward-declare the aggregate types named below, so the prototypes use the real
 * tags instead of throwaway function-prototype-scope tags. Zero behaviour/ABI
 * impact. */
struct tcpcb;
struct tcpiphdr;

int tcp_reass(register struct tcpcb * tp,
              register struct tcpiphdr * ti,
              struct mbuf * m);

void STKARGFUN tcp_input(register struct mbuf * m,
			 int iphlen);

void tcp_dooptions(struct tcpcb * tp,
                  struct mbuf * om,
                  struct tcpiphdr * ti,
                  int * ts_present,
                  u_long * ts_val,
                  u_long * ts_ecr);

void tcp_pulloutofband(struct socket * so,
                      struct tcpiphdr * ti,
                      register struct mbuf * m);

void tcp_xmit_timer(register struct tcpcb * tp, short rtt);

int tcp_mss(register struct tcpcb * tp,
            u_short offer);

