// Package blockchain implements a minimal proof-of-work blockchain,
// following "Building Blockchain in Go" (jeiwan.net), parts 1-3.
package blockchain

import (
	"bytes"
	"encoding/gob"
	"time"
)

// Block is one link in the chain: a timestamp, arbitrary data, the hash of
// the block before it, its own hash, and the nonce that made that hash
// satisfy the proof-of-work target.
type Block struct {
	Timestamp     int64
	Data          []byte
	PrevBlockHash []byte
	Hash          []byte
	Nonce         int
}

// NewBlock builds a block linked to prevHash and mines it: Nonce and Hash
// are set by running proof-of-work against the block's header bytes.
func NewBlock(data string, prevHash []byte) *Block {
	block := &Block{
		Timestamp:     time.Now().Unix(),
		Data:          []byte(data),
		PrevBlockHash: prevHash,
		Hash:          nil,
		Nonce:         0,
	}

	pow := NewProofOfWork(block)
	nonce, hash := pow.Run()

	block.Hash = hash
	block.Nonce = nonce

	return block
}

// NewGenesisBlock is the first block of a chain: no predecessor, so its
// PrevBlockHash is empty.
func NewGenesisBlock() *Block {
	return NewBlock("Genesis Block", []byte{})
}

// Serialize gob-encodes the block for on-disk persistence.
func (b *Block) Serialize() ([]byte, error) {
	var buf bytes.Buffer
	if err := gob.NewEncoder(&buf).Encode(b); err != nil {
		return nil, err
	}
	return buf.Bytes(), nil
}

// DeserializeBlock is the inverse of Serialize.
func DeserializeBlock(data []byte) (*Block, error) {
	var block Block
	if err := gob.NewDecoder(bytes.NewReader(data)).Decode(&block); err != nil {
		return nil, err
	}
	return &block, nil
}
