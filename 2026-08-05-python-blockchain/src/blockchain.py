"""The chain itself: genesis, proof-of-work mining, validation, and balances."""
import time

from .block import Block

MINING_REWARD = 10


class Blockchain:
    def __init__(self, difficulty=4):
        self.difficulty = difficulty
        self.pending_transactions = []
        self.chain = [self._create_genesis_block()]

    def _create_genesis_block(self):
        return Block(index=0, timestamp=0.0, transactions=[], previous_hash="0" * 64)

    @property
    def last_block(self):
        return self.chain[-1]

    def add_transaction(self, sender, recipient, amount):
        if amount <= 0:
            raise ValueError("Transaction amount must be positive")
        self.pending_transactions.append(
            {"sender": sender, "recipient": recipient, "amount": amount}
        )

    def proof_of_work(self, block):
        target = "0" * self.difficulty
        block.nonce = 0
        block.hash = block.compute_hash()
        while not block.hash.startswith(target):
            block.nonce += 1
            block.hash = block.compute_hash()
        return block.hash

    def mine_pending_transactions(self, miner_address):
        reward_tx = {"sender": "network", "recipient": miner_address, "amount": MINING_REWARD}
        transactions = self.pending_transactions + [reward_tx]

        new_block = Block(
            index=self.last_block.index + 1,
            timestamp=time.time(),
            transactions=transactions,
            previous_hash=self.last_block.hash,
        )
        self.proof_of_work(new_block)

        self.chain.append(new_block)
        self.pending_transactions = []
        return new_block

    def get_balance(self, address):
        balance = 0
        for block in self.chain:
            for tx in block.transactions:
                if tx["sender"] == address:
                    balance -= tx["amount"]
                if tx["recipient"] == address:
                    balance += tx["amount"]
        return balance

    def is_chain_valid(self):
        target = "0" * self.difficulty
        for i in range(1, len(self.chain)):
            current = self.chain[i]
            previous = self.chain[i - 1]

            if current.hash != current.compute_hash():
                return False, f"Block {current.index} hash does not match its own content"

            if current.previous_hash != previous.hash:
                return False, f"Block {current.index} does not link to block {previous.index}"

            if not current.hash.startswith(target):
                return False, f"Block {current.index} does not satisfy proof-of-work difficulty"

        return True, "Chain is valid"
