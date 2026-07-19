/* Prototypes for functions defined in
if.c
 */

/* Forward-declare the aggregate types named below, so the prototypes use the real
 * tags instead of throwaway function-prototype-scope tags. Zero behaviour/ABI
 * impact. */
struct socket;
struct ifqueue;

void ifinit(void);

void if_attach(struct ifnet * ifp);

struct ifaddr * ifa_ifwithaddr(register struct sockaddr * addr);

struct ifaddr * ifa_ifwithdstaddr(register struct sockaddr * addr);

struct ifaddr * ifa_ifwithnet(struct sockaddr * addr);

struct ifaddr * ifa_ifwithaf(register int af);

struct ifaddr * ifaof_ifpforaddr(struct sockaddr * addr,
                                 register struct ifnet * ifp);

void link_rtrequest(int cmd,
                   struct rtentry * rt,
                   struct sockaddr * sa);

void if_down(register struct ifnet * ifp);

void if_qflush(register struct ifqueue * ifq);

void if_slowtimo(void);

struct ifnet * ifunit(register char * name);

int ifioctl(struct socket * so,
            int cmd,
            caddr_t data);

int ifconf(int cmd,
           caddr_t data);

