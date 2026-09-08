package blockchain

import "testing"

func TestNewGenesisBlockHasNoPredecessor(t *testing.T) {
	genesis := NewGenesisBlock()

	if len(genesis.PrevBlockHash) != 0 {
		t.Fatalf("genesis block PrevBlockHash = %x, want empty", genesis.PrevBlockHash)
	}
	if string(genesis.Data) != "Genesis Block" {
		t.Fatalf("genesis block Data = %q, want %q", genesis.Data, "Genesis Block")
	}
}

func TestNewBlockLinksToPrevHash(t *testing.T) {
	genesis := NewGenesisBlock()
	next := NewBlock("second block", genesis.Hash)

	if string(next.PrevBlockHash) != string(genesis.Hash) {
		t.Fatalf("next.PrevBlockHash = %x, want genesis.Hash %x", next.PrevBlockHash, genesis.Hash)
	}
}

func TestSerializeRoundTrip(t *testing.T) {
	original := NewBlock("round trip me", []byte("prev"))

	data, err := original.Serialize()
	if err != nil {
		t.Fatalf("Serialize: %v", err)
	}

	restored, err := DeserializeBlock(data)
	if err != nil {
		t.Fatalf("DeserializeBlock: %v", err)
	}

	if string(restored.Data) != string(original.Data) {
		t.Errorf("Data = %q, want %q", restored.Data, original.Data)
	}
	if string(restored.Hash) != string(original.Hash) {
		t.Errorf("Hash = %x, want %x", restored.Hash, original.Hash)
	}
	if restored.Nonce != original.Nonce {
		t.Errorf("Nonce = %d, want %d", restored.Nonce, original.Nonce)
	}
	if restored.Timestamp != original.Timestamp {
		t.Errorf("Timestamp = %d, want %d", restored.Timestamp, original.Timestamp)
	}
}
