//! A tiny seeded PRNG (xorshift64*), so a game is fully reproducible from
//! its seed. That is what lets the tests replay whole games exactly.

#[derive(Clone, Debug)]
pub struct Rng(u64);

impl Rng {
    pub fn new(seed: u64) -> Rng {
        // Run the seed through one round of splitmix64 so nearby seeds give
        // unrelated streams, and force a non-zero state: xorshift stuck at
        // zero would stay zero forever.
        let mut z = seed.wrapping_add(0x9E37_79B9_7F4A_7C15);
        z = (z ^ (z >> 30)).wrapping_mul(0xBF58_476D_1CE4_E5B9);
        z = (z ^ (z >> 27)).wrapping_mul(0x94D0_49BB_1331_11EB);
        Rng((z ^ (z >> 31)) | 1)
    }

    pub fn next_u32(&mut self) -> u32 {
        let mut x = self.0;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        self.0 = x;
        (x.wrapping_mul(0x2545_F491_4F6C_DD1D) >> 32) as u32
    }

    /// A value in `0..n`. `n` must be non-zero.
    pub fn below(&mut self, n: u32) -> u32 {
        assert!(n > 0);
        // Lemire's multiply-shift: unbiased enough for a game, no division.
        ((self.next_u32() as u64 * n as u64) >> 32) as u32
    }

    pub fn chance(&mut self, percent: u32) -> bool {
        self.below(100) < percent
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn same_seed_same_sequence() {
        let (mut a, mut b) = (Rng::new(42), Rng::new(42));
        for _ in 0..100 {
            assert_eq!(a.next_u32(), b.next_u32());
        }
        // Seeds differing only in the lowest bit must not collide.
        for seed in 0..64u64 {
            assert_ne!(
                Rng::new(seed * 2).next_u32(),
                Rng::new(seed * 2 + 1).next_u32()
            );
        }
    }

    #[test]
    fn below_stays_in_range_and_covers_it() {
        let mut r = Rng::new(0);
        let mut seen = [false; 7];
        for _ in 0..1000 {
            let v = r.below(7);
            assert!(v < 7);
            seen[v as usize] = true;
        }
        assert!(seen.iter().all(|&s| s));
    }
}
