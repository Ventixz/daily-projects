#ifndef PRIME_H
#define PRIME_H

/* Returns: 1 if x is prime, 0 if not, -1 if x < 2 (undefined). */
int is_prime(int x);

/* Returns the next prime after x, or x itself if x is already prime. */
int next_prime(int x);

#endif
