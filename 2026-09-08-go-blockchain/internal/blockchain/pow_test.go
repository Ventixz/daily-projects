package blockchain

import (
	"math/big"
	"testing"
)

func TestRunProducesHashBelowTarget(t *testing.T) {
	block := NewBlock("test data", []byte("prev-hash"))
	pow := NewProofOfWork(block)

	var hashInt big.Int
	hashInt.SetBytes(block.Hash)

	if hashInt.Cmp(pow.target) != -1 {
		t.Fatalf("mined hash %x does not satisfy target %x", block.Hash, pow.target.Bytes())
	}
}

func TestValidateAcceptsAMinedBlock(t *testing.T) {
	block := NewBlock("test data", []byte("prev-hash"))

	if !NewProofOfWork(block).Validate() {
		t.Fatal("Validate rejected a block that was just mined by Run")
	}
}

func TestValidateRejectsTamperedData(t *testing.T) {
	block := NewBlock("original data", []byte("prev-hash"))

	block.Data = []byte("tampered data")

	if NewProofOfWork(block).Validate() {
		t.Fatal("Validate accepted a block whose Data changed after mining")
	}
}

func TestValidateRejectsTamperedNonce(t *testing.T) {
	block := NewBlock("original data", []byte("prev-hash"))

	block.Nonce++

	if NewProofOfWork(block).Validate() {
		t.Fatal("Validate accepted a block whose Nonce changed after mining")
	}
}

func TestDifferentDataProducesDifferentHash(t *testing.T) {
	a := NewBlock("data A", []byte("prev-hash"))
	b := NewBlock("data B", []byte("prev-hash"))

	if string(a.Hash) == string(b.Hash) {
		t.Fatal("two blocks with different data mined to the same hash")
	}
}
