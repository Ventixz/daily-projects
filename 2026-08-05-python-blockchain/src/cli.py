"""Interactive REPL so the chain's internals are directly observable."""
from .blockchain import Blockchain

HELP = """\
Commands:
  send <from> <to> <amount>   queue a transaction
  mine <miner_address>        mine all pending transactions into a new block
  balance <address>           show an address's balance
  chain                       print every block in the chain
  pending                     show queued (unmined) transactions
  validate                    check the chain's integrity
  tamper <index> <amount>     rewrite a mined block's first tx amount (demo only)
  help                        show this message
  quit                        exit
"""


def print_chain(chain):
    for block in chain.chain:
        print(block)
        for tx in block.transactions:
            print(f"    {tx['sender']} -> {tx['recipient']}: {tx['amount']}")


def run(input_stream=None):
    chain = Blockchain(difficulty=4)
    print("Simple Blockchain REPL. Type 'help' for commands.")

    reader = input_stream or (lambda: input("chain> "))

    while True:
        try:
            line = reader()
        except EOFError:
            break
        if line is None:
            break

        parts = line.strip().split()
        if not parts:
            continue
        cmd, args = parts[0], parts[1:]

        try:
            if cmd == "quit":
                break
            elif cmd == "help":
                print(HELP)
            elif cmd == "send":
                sender, recipient, amount = args[0], args[1], float(args[2])
                chain.add_transaction(sender, recipient, amount)
                print(f"Queued: {sender} -> {recipient}: {amount}")
            elif cmd == "mine":
                block = chain.mine_pending_transactions(args[0])
                print(f"Mined {block}")
            elif cmd == "balance":
                print(f"{args[0]}: {chain.get_balance(args[0])}")
            elif cmd == "chain":
                print_chain(chain)
            elif cmd == "pending":
                print(chain.pending_transactions)
            elif cmd == "validate":
                valid, reason = chain.is_chain_valid()
                print(f"{'VALID' if valid else 'INVALID'}: {reason}")
            elif cmd == "tamper":
                index, amount = int(args[0]), float(args[1])
                chain.chain[index].transactions[0]["amount"] = amount
                print(f"Tampered block {index} (hash left unrecomputed, like a real attacker)")
            else:
                print(f"Unknown command: {cmd}. Type 'help'.")
        except (IndexError, ValueError) as exc:
            print(f"Bad arguments for '{cmd}': {exc}")


if __name__ == "__main__":
    run()
