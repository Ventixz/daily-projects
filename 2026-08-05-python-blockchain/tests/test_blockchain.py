import unittest

from src.blockchain import Blockchain, MINING_REWARD


class TestGenesisBlock(unittest.TestCase):
    def test_chain_starts_with_one_block(self):
        chain = Blockchain(difficulty=2)
        self.assertEqual(len(chain.chain), 1)
        self.assertEqual(chain.chain[0].index, 0)
        self.assertEqual(chain.chain[0].previous_hash, "0" * 64)


class TestTransactionsAndMining(unittest.TestCase):
    def test_mining_moves_pending_transactions_into_a_block(self):
        chain = Blockchain(difficulty=2)
        chain.add_transaction("alice", "bob", 5)
        chain.add_transaction("bob", "carol", 2)
        self.assertEqual(len(chain.pending_transactions), 2)

        block = chain.mine_pending_transactions("miner1")

        self.assertEqual(chain.pending_transactions, [])
        # 2 queued txs + 1 mining reward tx
        self.assertEqual(len(block.transactions), 3)
        self.assertEqual(len(chain.chain), 2)

    def test_mined_block_links_to_previous_hash(self):
        chain = Blockchain(difficulty=2)
        chain.add_transaction("alice", "bob", 5)
        block = chain.mine_pending_transactions("miner1")
        self.assertEqual(block.previous_hash, chain.chain[0].hash)

    def test_negative_or_zero_amount_rejected(self):
        chain = Blockchain(difficulty=2)
        with self.assertRaises(ValueError):
            chain.add_transaction("alice", "bob", 0)
        with self.assertRaises(ValueError):
            chain.add_transaction("alice", "bob", -5)


class TestProofOfWork(unittest.TestCase):
    def test_mined_block_hash_satisfies_difficulty(self):
        chain = Blockchain(difficulty=3)
        chain.add_transaction("alice", "bob", 1)
        block = chain.mine_pending_transactions("miner1")
        self.assertTrue(block.hash.startswith("0" * 3))

    def test_recomputing_hash_matches_stored_hash(self):
        chain = Blockchain(difficulty=2)
        chain.add_transaction("alice", "bob", 1)
        block = chain.mine_pending_transactions("miner1")
        self.assertEqual(block.hash, block.compute_hash())


class TestBalances(unittest.TestCase):
    def test_balance_reflects_sends_and_mining_reward(self):
        chain = Blockchain(difficulty=2)
        chain.add_transaction("alice", "bob", 10)
        chain.mine_pending_transactions("miner1")

        self.assertEqual(chain.get_balance("alice"), -10)
        self.assertEqual(chain.get_balance("bob"), 10)
        self.assertEqual(chain.get_balance("miner1"), MINING_REWARD)

    def test_balance_accumulates_across_multiple_blocks(self):
        chain = Blockchain(difficulty=2)
        chain.add_transaction("alice", "bob", 10)
        chain.mine_pending_transactions("miner1")
        chain.add_transaction("bob", "carol", 4)
        chain.mine_pending_transactions("miner1")

        self.assertEqual(chain.get_balance("bob"), 10 - 4)
        self.assertEqual(chain.get_balance("miner1"), MINING_REWARD * 2)


class TestChainValidation(unittest.TestCase):
    def test_untouched_chain_is_valid(self):
        chain = Blockchain(difficulty=2)
        chain.add_transaction("alice", "bob", 5)
        chain.mine_pending_transactions("miner1")

        valid, _ = chain.is_chain_valid()
        self.assertTrue(valid)

    def test_tampering_with_transaction_amount_invalidates_chain(self):
        chain = Blockchain(difficulty=2)
        chain.add_transaction("alice", "bob", 5)
        chain.mine_pending_transactions("miner1")

        # Rewrite history without recomputing the hash, like a real attacker would try.
        chain.chain[1].transactions[0]["amount"] = 5000

        valid, reason = chain.is_chain_valid()
        self.assertFalse(valid)
        self.assertIn("does not match its own content", reason)

    def test_breaking_the_previous_hash_link_invalidates_chain(self):
        chain = Blockchain(difficulty=2)
        chain.add_transaction("alice", "bob", 5)
        chain.mine_pending_transactions("miner1")
        chain.add_transaction("bob", "carol", 1)
        chain.mine_pending_transactions("miner1")

        # Rewrite both previous_hash and hash so the block is internally
        # self-consistent but no longer actually linked to its predecessor.
        chain.chain[1].previous_hash = "f" * 64
        chain.chain[1].hash = chain.chain[1].compute_hash()

        valid, reason = chain.is_chain_valid()
        self.assertFalse(valid)
        self.assertIn("does not link to", reason)


if __name__ == "__main__":
    unittest.main()
