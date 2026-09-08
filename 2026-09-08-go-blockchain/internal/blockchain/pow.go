package blockchain

import (
	"bytes"
	"crypto/sha256"
	"encoding/binary"
	"math"
	"math/big"
)

// targetBits sets mining difficulty: the block hash, read as a big.Int,
// must be smaller than 1 << (256 - targetBits) — i.e. its first targetBits
// bits must be zero. 16 keeps a demo/test run to well under a second while
// still doing real work (as opposed to targetBits=0, which every hash
// would trivially satisfy).
const targetBits = 16

// maxNonce bounds the search so a block that can't be mined (shouldn't
// happen at this difficulty) fails loudly instead of spinning forever.
const maxNonce = math.MaxInt64

// ProofOfWork ties a block to the difficulty target it must be mined
// against.
type ProofOfWork struct {
	block  *Block
	target *big.Int
}

// NewProofOfWork builds the target 1<<(256-targetBits) once per block, so
// Run and Validate compare against the same value.
func NewProofOfWork(b *Block) *ProofOfWork {
	target := big.NewInt(1)
	target.Lsh(target, uint(256-targetBits))
	return &ProofOfWork{block: b, target: target}
}

// prepareData concatenates everything the hash must commit to: the chain
// link (PrevBlockHash), the payload (Data, Timestamp), the difficulty
// (targetBits, so a block mined under a different difficulty produces a
// different hash), and the candidate nonce.
func (pow *ProofOfWork) prepareData(nonce int) []byte {
	return bytes.Join(
		[][]byte{
			pow.block.PrevBlockHash,
			pow.block.Data,
			intToBytes(pow.block.Timestamp),
			intToBytes(int64(targetBits)),
			intToBytes(int64(nonce)),
		},
		[]byte{},
	)
}

// Run searches nonces from 0 upward until sha256(data+nonce), read as a
// big-endian integer, is below the target. Returns the winning nonce and
// the hash it produced.
func (pow *ProofOfWork) Run() (int, []byte) {
	var hashInt big.Int
	var hash [32]byte
	nonce := 0

	for nonce < maxNonce {
		data := pow.prepareData(nonce)
		hash = sha256.Sum256(data)
		hashInt.SetBytes(hash[:])

		if hashInt.Cmp(pow.target) == -1 {
			break
		}
		nonce++
	}

	return nonce, hash[:]
}

// Validate re-derives the hash from the block's own stored Nonce and
// checks it both matches Block.Hash and still meets the target — so a
// tampered Data or Nonce is caught even if Hash was left unchanged.
func (pow *ProofOfWork) Validate() bool {
	var hashInt big.Int

	data := pow.prepareData(pow.block.Nonce)
	hash := sha256.Sum256(data)
	hashInt.SetBytes(hash[:])

	return hashInt.Cmp(pow.target) == -1 && bytes.Equal(hash[:], pow.block.Hash)
}

func intToBytes(n int64) []byte {
	buf := make([]byte, 8)
	binary.BigEndian.PutUint64(buf, uint64(n))
	return buf
}
