package password

import (
	"strings"
	"testing"
)

const testIters = 1000 // keep tests fast; production uses DefaultIterations

func TestHashVerifyRoundTrip(t *testing.T) {
	h, err := Hash("correct horse battery staple", testIters)
	if err != nil {
		t.Fatal(err)
	}
	if !strings.HasPrefix(h, "pbkdf2-sha256$1000$") {
		t.Fatalf("unexpected encoding %q", h)
	}
	if ok, err := Verify("correct horse battery staple", h); !ok || err != nil {
		t.Fatalf("Verify(correct) = %v, %v", ok, err)
	}
	if ok, _ := Verify("correct horse battery stapler", h); ok {
		t.Fatal("wrong password accepted")
	}
	if ok, _ := Verify("", h); ok {
		t.Fatal("empty password accepted")
	}
}

func TestSaltMakesHashesUnique(t *testing.T) {
	a, _ := Hash("same", testIters)
	b, _ := Hash("same", testIters)
	if a == b {
		t.Fatal("two hashes of the same password are identical: salt not random")
	}
}

func TestTamperedHashFails(t *testing.T) {
	h, _ := Hash("pw-123456", testIters)
	parts := strings.Split(h, "$")
	// Change the iteration count: the derived key no longer matches.
	parts[1] = "1001"
	if ok, _ := Verify("pw-123456", strings.Join(parts, "$")); ok {
		t.Fatal("hash with altered iterations still verified")
	}
}

func TestMalformedAndHostileHashes(t *testing.T) {
	good, _ := Hash("x", testIters)
	salt := strings.Split(good, "$")[2]
	key := strings.Split(good, "$")[3]
	cases := []string{
		"",
		"plaintext",
		"bcrypt$10$" + salt + "$" + key,
		"pbkdf2-sha256$abc$" + salt + "$" + key,
		"pbkdf2-sha256$0$" + salt + "$" + key,
		"pbkdf2-sha256$999999999$" + salt + "$" + key, // would burn minutes of CPU
		"pbkdf2-sha256$1000$!!!$" + key,
		"pbkdf2-sha256$1000$" + salt + "$c2hvcnQ", // key too short
	}
	for _, c := range cases {
		if ok, err := Verify("x", c); ok || err != ErrMalformed {
			t.Errorf("Verify(%q) = %v, %v; want false, ErrMalformed", c, ok, err)
		}
	}
	if _, err := Hash("x", 0); err == nil {
		t.Error("Hash accepted 0 iterations")
	}
}

func TestNeedsRehash(t *testing.T) {
	h, _ := Hash("x", testIters)
	if NeedsRehash(h, testIters) {
		t.Error("hash at current cost flagged for rehash")
	}
	if !NeedsRehash(h, testIters*2) {
		t.Error("weaker hash not flagged for rehash")
	}
	if !NeedsRehash("garbage", testIters) {
		t.Error("unparseable hash not flagged")
	}
}
