package blockchain

import (
	"bytes"
	"encoding/gob"
	"errors"
	"os"
)

// Blockchain is an in-memory chain of blocks, newest first is not how it's
// stored — Blocks[0] is the genesis block, Blocks[len-1] is the tip — but
// AddBlock and Validate both only ever need the tip, so ordering direction
// is an implementation detail, not part of the public contract.
type Blockchain struct {
	Blocks []*Block
}

// NewBlockchain starts a fresh chain containing only the genesis block.
func NewBlockchain() *Blockchain {
	return &Blockchain{Blocks: []*Block{NewGenesisBlock()}}
}

// AddBlock mines a new block on top of the current tip and appends it.
func (bc *Blockchain) AddBlock(data string) *Block {
	prevBlock := bc.Blocks[len(bc.Blocks)-1]
	newBlock := NewBlock(data, prevBlock.Hash)
	bc.Blocks = append(bc.Blocks, newBlock)
	return newBlock
}

// ErrInvalidChain is returned by IsValid when a link is broken.
var ErrInvalidChain = errors.New("blockchain: invalid chain")

// IsValid walks the chain from the genesis block forward and checks two
// things per block: its proof-of-work still satisfies the difficulty
// target, and its PrevBlockHash actually matches the previous block's
// Hash. Either check failing means the chain was tampered with (or
// corrupted) after being mined.
func (bc *Blockchain) IsValid() error {
	if len(bc.Blocks) == 0 {
		return ErrInvalidChain
	}

	for i, block := range bc.Blocks {
		if !NewProofOfWork(block).Validate() {
			return ErrInvalidChain
		}
		if i == 0 {
			continue
		}
		if !bytes.Equal(block.PrevBlockHash, bc.Blocks[i-1].Hash) {
			return ErrInvalidChain
		}
	}

	return nil
}

// SaveToFile gob-encodes the whole chain to path, so a CLI invocation can
// pick up where the last one left off instead of re-mining the genesis
// block every time.
func (bc *Blockchain) SaveToFile(path string) error {
	var buf bytes.Buffer
	if err := gob.NewEncoder(&buf).Encode(bc); err != nil {
		return err
	}
	return os.WriteFile(path, buf.Bytes(), 0o600)
}

// LoadFromFile is the inverse of SaveToFile. A missing file is not an
// error: it means "no chain persisted yet," and the caller is expected to
// fall back to NewBlockchain.
func LoadFromFile(path string) (*Blockchain, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}

	var bc Blockchain
	if err := gob.NewDecoder(bytes.NewReader(data)).Decode(&bc); err != nil {
		return nil, err
	}
	return &bc, nil
}
