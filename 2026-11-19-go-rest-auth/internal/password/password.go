// Package password hashes and verifies passwords with PBKDF2-HMAC-SHA256.
//
// The tutorial uses bcrypt from golang.org/x/crypto. This package sticks to the
// standard library (crypto/pbkdf2 arrived in Go 1.24) so the project has no
// dependencies, but the ideas are the same: a slow, salted, tunable hash stored
// together with the parameters needed to check it later.
//
// Encoded form (one line, safe to keep in a text file):
//
//	pbkdf2-sha256$<iterations>$<salt, base64>$<derived key, base64>
package password

import (
	"crypto/pbkdf2"
	"crypto/rand"
	"crypto/sha256"
	"crypto/subtle"
	"encoding/base64"
	"errors"
	"fmt"
	"strconv"
	"strings"
)

const (
	scheme = "pbkdf2-sha256"

	// DefaultIterations follows OWASP's recommendation for PBKDF2-HMAC-SHA256.
	DefaultIterations = 600_000

	// MaxIterations caps what Verify will accept from a stored hash, so a
	// tampered users file can't make one login take minutes of CPU.
	MaxIterations = 10_000_000

	saltLen = 16
	keyLen  = 32
)

var b64 = base64.RawStdEncoding

// ErrMalformed is returned when an encoded hash can't be parsed.
var ErrMalformed = errors.New("password: malformed hash")

// Hash derives a key from pw with a fresh random salt and returns the encoded
// string. iterations must be at least 1; use DefaultIterations in production.
func Hash(pw string, iterations int) (string, error) {
	if iterations < 1 || iterations > MaxIterations {
		return "", fmt.Errorf("password: iterations %d out of range", iterations)
	}
	salt := make([]byte, saltLen)
	if _, err := rand.Read(salt); err != nil {
		return "", err
	}
	key, err := pbkdf2.Key(sha256.New, pw, salt, iterations, keyLen)
	if err != nil {
		return "", err
	}
	return fmt.Sprintf("%s$%d$%s$%s", scheme, iterations, b64.EncodeToString(salt), b64.EncodeToString(key)), nil
}

type parsed struct {
	iterations int
	salt, key  []byte
}

func parse(encoded string) (parsed, error) {
	parts := strings.Split(encoded, "$")
	if len(parts) != 4 || parts[0] != scheme {
		return parsed{}, ErrMalformed
	}
	n, err := strconv.Atoi(parts[1])
	if err != nil || n < 1 || n > MaxIterations {
		return parsed{}, ErrMalformed
	}
	salt, err := b64.DecodeString(parts[2])
	if err != nil || len(salt) < 8 {
		return parsed{}, ErrMalformed
	}
	key, err := b64.DecodeString(parts[3])
	if err != nil || len(key) < 16 {
		return parsed{}, ErrMalformed
	}
	return parsed{n, salt, key}, nil
}

// Verify reports whether pw matches the encoded hash. The final comparison is
// constant-time so the response time doesn't reveal how many bytes matched.
func Verify(pw, encoded string) (bool, error) {
	p, err := parse(encoded)
	if err != nil {
		return false, err
	}
	key, err := pbkdf2.Key(sha256.New, pw, p.salt, p.iterations, len(p.key))
	if err != nil {
		return false, err
	}
	return subtle.ConstantTimeCompare(key, p.key) == 1, nil
}

// Iterations returns the iteration count stored in an encoded hash.
func Iterations(encoded string) (int, error) {
	p, err := parse(encoded)
	return p.iterations, err
}

// NeedsRehash reports whether a stored hash is weaker than the current
// policy, so it can be upgraded the next time the user logs in successfully.
func NeedsRehash(encoded string, iterations int) bool {
	p, err := parse(encoded)
	return err != nil || p.iterations < iterations
}
