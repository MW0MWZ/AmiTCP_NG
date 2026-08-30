/* Prototypes for functions defined in
in_cksum_copy.c
 */

u_long in_cksum_copy(const void *src, void *dst, u_long len,
                     u_long sum, int *odd);

u_short in_cksum_fold(u_long sum);

/* Unfolded, uncomplemented sum of a flat byte run -- for the receive consumers'
 * pseudo-header. See the comment at the definition for why not in_cksum(). */
u_long in_cksum_words(const void *buf, u_long len, u_long sum);

/* Same contract, hand-written 68k -- src/netinet/in_cksum_copy_asm.S. The C above is
 * its fuzz oracle; if the two ever disagree, the C is right. */
u_long in_cksum_copy_asm(const void *src, void *dst, u_long len,
                         u_long sum, int *odd);
