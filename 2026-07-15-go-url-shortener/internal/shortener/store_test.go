package shortener

import (
	"path/filepath"
	"testing"
)

func TestCreateAndGet(t *testing.T) {
	store, err := NewStore(filepath.Join(t.TempDir(), "links.json"))
	if err != nil {
		t.Fatalf("NewStore: %v", err)
	}

	link, err := store.Create("https://example.com/path", "")
	if err != nil {
		t.Fatalf("Create: %v", err)
	}
	if len(link.Code) != codeLength {
		t.Errorf("code length = %d, want %d", len(link.Code), codeLength)
	}

	got, ok := store.Get(link.Code)
	if !ok {
		t.Fatalf("Get(%q) not found", link.Code)
	}
	if got.URL != "https://example.com/path" {
		t.Errorf("URL = %q, want the original URL", got.URL)
	}
	if got.Clicks != 0 {
		t.Errorf("Clicks = %d, want 0", got.Clicks)
	}
}

func TestCreateRejectsInvalidURL(t *testing.T) {
	store, _ := NewStore(filepath.Join(t.TempDir(), "links.json"))

	cases := []string{"", "not a url", "example.com/no-scheme", "javascript:alert(1)", "ftp://example.com/file"}
	for _, raw := range cases {
		if _, err := store.Create(raw, ""); err == nil {
			t.Errorf("Create(%q) succeeded, want error", raw)
		}
	}
}

func TestCreateCustomCode(t *testing.T) {
	store, _ := NewStore(filepath.Join(t.TempDir(), "links.json"))

	link, err := store.Create("https://example.com", "my-code")
	if err != nil {
		t.Fatalf("Create: %v", err)
	}
	if link.Code != "my-code" {
		t.Errorf("Code = %q, want %q", link.Code, "my-code")
	}

	if _, err := store.Create("https://example.org", "my-code"); err != ErrCodeTaken {
		t.Errorf("Create with duplicate custom code: err = %v, want ErrCodeTaken", err)
	}
}

func TestRecordClickIncrements(t *testing.T) {
	store, _ := NewStore(filepath.Join(t.TempDir(), "links.json"))
	link, _ := store.Create("https://example.com", "")

	for i := 0; i < 3; i++ {
		if err := store.RecordClick(link.Code); err != nil {
			t.Fatalf("RecordClick: %v", err)
		}
	}

	got, _ := store.Get(link.Code)
	if got.Clicks != 3 {
		t.Errorf("Clicks = %d, want 3", got.Clicks)
	}
}

func TestRecordClickUnknownCode(t *testing.T) {
	store, _ := NewStore(filepath.Join(t.TempDir(), "links.json"))
	if err := store.RecordClick("nope"); err != ErrCodeNotFound {
		t.Errorf("err = %v, want ErrCodeNotFound", err)
	}
}

func TestPersistenceAcrossRestarts(t *testing.T) {
	path := filepath.Join(t.TempDir(), "links.json")

	store1, _ := NewStore(path)
	link, err := store1.Create("https://example.com/persisted", "")
	if err != nil {
		t.Fatalf("Create: %v", err)
	}

	store2, err := NewStore(path)
	if err != nil {
		t.Fatalf("NewStore (reload): %v", err)
	}
	got, ok := store2.Get(link.Code)
	if !ok {
		t.Fatalf("link %q missing after reload", link.Code)
	}
	if got.URL != link.URL {
		t.Errorf("URL after reload = %q, want %q", got.URL, link.URL)
	}
}
