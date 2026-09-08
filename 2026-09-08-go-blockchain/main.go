// Command blockchain is a CLI over the proof-of-work chain in
// internal/blockchain: "addblock" mines a new block onto the persisted
// chain (creating it on first run), "printchain" walks the chain and
// prints every block plus whether its own proof-of-work still checks out.
package main

import (
	"flag"
	"fmt"
	"os"

	"blockchain/internal/blockchain"
)

const dbFile = "blockchain.db"

func main() {
	if len(os.Args) < 2 {
		usage()
		os.Exit(1)
	}

	switch os.Args[1] {
	case "addblock":
		fs := flag.NewFlagSet("addblock", flag.ExitOnError)
		data := fs.String("data", "", "block data")
		fs.Parse(os.Args[2:])
		if *data == "" {
			fmt.Fprintln(os.Stderr, "addblock: -data is required")
			os.Exit(1)
		}
		addBlock(*data)
	case "printchain":
		printChain()
	default:
		usage()
		os.Exit(1)
	}
}

func usage() {
	fmt.Fprintln(os.Stderr, `usage:
  blockchain addblock -data "BLOCK_DATA"   mine a new block onto the chain
  blockchain printchain                     print every block in the chain`)
}

func openOrCreateChain() *blockchain.Blockchain {
	bc, err := blockchain.LoadFromFile(dbFile)
	if err != nil {
		bc = blockchain.NewBlockchain()
	}
	return bc
}

func addBlock(data string) {
	bc := openOrCreateChain()
	block := bc.AddBlock(data)

	if err := bc.SaveToFile(dbFile); err != nil {
		fmt.Fprintf(os.Stderr, "saving chain: %v\n", err)
		os.Exit(1)
	}

	fmt.Printf("mined block %x (nonce %d)\n", block.Hash, block.Nonce)
}

func printChain() {
	bc := openOrCreateChain()

	for i, block := range bc.Blocks {
		valid := blockchain.NewProofOfWork(block).Validate()
		fmt.Printf("Block %d\n", i)
		fmt.Printf("  Data:  %s\n", block.Data)
		fmt.Printf("  Hash:  %x\n", block.Hash)
		fmt.Printf("  Prev:  %x\n", block.PrevBlockHash)
		fmt.Printf("  Nonce: %d\n", block.Nonce)
		fmt.Printf("  PoW valid: %v\n\n", valid)
	}
}
