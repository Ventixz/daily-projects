package blockchain

import (
	"path/filepath"
	"testing"
)

func TestNewBlockchainStartsWithGenesisOnly(t *testing.T) {
	bc := NewBlockchain()

	if len(bc.Blocks) != 1 {
		t.Fatalf("len(bc.Blocks) = %d, want 1", len(bc.Blocks))
	}
	if string(bc.Blocks[0].Data) != "Genesis Block" {
		t.Fatalf("bc.Blocks[0].Data = %q, want %q", bc.Blocks[0].Data, "Genesis Block")
	}
}

func TestAddBlockExtendsChainAndLinksHashes(t *testing.T) {
	bc := NewBlockchain()

	bc.AddBlock("Send 1 BTC to Ivan")
	bc.AddBlock("Send 2 more BTC to Ivan")

	if len(bc.Blocks) != 3 {
		t.Fatalf("len(bc.Blocks) = %d, want 3", len(bc.Blocks))
	}

	for i := 1; i < len(bc.Blocks); i++ {
		if string(bc.Blocks[i].PrevBlockHash) != string(bc.Blocks[i-1].Hash) {
			t.Fatalf("block %d PrevBlockHash does not match block %d Hash", i, i-1)
		}
	}
}

func TestIsValidAcceptsAFreshChain(t *testing.T) {
	bc := NewBlockchain()
	bc.AddBlock("a")
	bc.AddBlock("b")

	if err := bc.IsValid(); err != nil {
		t.Fatalf("IsValid() = %v, want nil", err)
	}
}

func TestIsValidCatchesADataSwapBetweenBlocks(t *testing.T) {
	bc := NewBlockchain()
	bc.AddBlock("original payload")
	bc.AddBlock("b")

	// Swap in a payload that was never mined into block 1: this keeps
	// PrevBlockHash chaining intact but breaks that block's own
	// proof-of-work, which is exactly the tamper IsValid must catch.
	bc.Blocks[1].Data = []byte("forged payload")

	if err := bc.IsValid(); err == nil {
		t.Fatal("IsValid() = nil, want an error after a block's data was swapped")
	}
}

func TestIsValidCatchesABrokenLink(t *testing.T) {
	bc := NewBlockchain()
	bc.AddBlock("a")
	bc.AddBlock("b")

	bc.Blocks[2].PrevBlockHash = []byte("not the real previous hash")

	if err := bc.IsValid(); err == nil {
		t.Fatal("IsValid() = nil, want an error after PrevBlockHash was broken")
	}
}

func TestSaveAndLoadRoundTrip(t *testing.T) {
	bc := NewBlockchain()
	bc.AddBlock("persist me")

	path := filepath.Join(t.TempDir(), "chain.db")
	if err := bc.SaveToFile(path); err != nil {
		t.Fatalf("SaveToFile: %v", err)
	}

	loaded, err := LoadFromFile(path)
	if err != nil {
		t.Fatalf("LoadFromFile: %v", err)
	}

	if len(loaded.Blocks) != len(bc.Blocks) {
		t.Fatalf("loaded %d blocks, want %d", len(loaded.Blocks), len(bc.Blocks))
	}
	if err := loaded.IsValid(); err != nil {
		t.Fatalf("loaded chain IsValid() = %v, want nil", err)
	}
}

func TestLoadFromFileMissingFileErrors(t *testing.T) {
	_, err := LoadFromFile(filepath.Join(t.TempDir(), "does-not-exist.db"))
	if err == nil {
		t.Fatal("LoadFromFile on a missing file = nil error, want non-nil")
	}
}
