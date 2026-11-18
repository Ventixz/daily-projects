package main

import (
	"bufio"
	"crypto/pbkdf2"
	"crypto/rand"
	"crypto/sha256"
	"crypto/subtle"
	"encoding/base64"
	"errors"
	"fmt"
	"io"
	"strconv"
	"strings"
)

// DefaultIterations follows the OWASP recommendation for PBKDF2-HMAC-SHA256.
const DefaultIterations = 600_000

const (
	saltLen = 16
	keyLen  = 32
	hashTag = "pbkdf2-sha256"
	minIter = 1_000
)

var b64 = base64.RawStdEncoding

// ErrMalformedHash is returned when a stored hash string can't be parsed.
var ErrMalformedHash = errors.New("malformed password hash")

// HashPassword returns a self-describing hash string:
//
//	pbkdf2-sha256$<iterations>$<salt b64>$<key b64>
//
// Storing the algorithm and cost next to the hash means the cost can be raised
// later without breaking existing users.
func HashPassword(password string, iterations int) (string, error) {
	if iterations < minIter {
		return "", fmt.Errorf("iterations must be >= %d", minIter)
	}
	salt := make([]byte, saltLen)
	if _, err := rand.Read(salt); err != nil {
		return "", err
	}
	key, err := pbkdf2.Key(sha256.New, password, salt, iterations, keyLen)
	if err != nil {
		return "", err
	}
	return fmt.Sprintf("%s$%d$%s$%s", hashTag, iterations, b64.EncodeToString(salt), b64.EncodeToString(key)), nil
}

type parsedHash struct {
	iter      int
	salt, key []byte
}

func parseHash(encoded string) (parsedHash, error) {
	parts := strings.Split(encoded, "$")
	if len(parts) != 4 || parts[0] != hashTag {
		return parsedHash{}, ErrMalformedHash
	}
	iter, err := strconv.Atoi(parts[1])
	if err != nil || iter < minIter {
		return parsedHash{}, ErrMalformedHash
	}
	salt, err := b64.DecodeString(parts[2])
	if err != nil || len(salt) == 0 {
		return parsedHash{}, ErrMalformedHash
	}
	key, err := b64.DecodeString(parts[3])
	if err != nil || len(key) == 0 {
		return parsedHash{}, ErrMalformedHash
	}
	return parsedHash{iter, salt, key}, nil
}

// VerifyPassword reports whether password matches the encoded hash. The final
// comparison is constant-time so the response time doesn't reveal how many
// leading bytes matched.
func VerifyPassword(password, encoded string) (bool, error) {
	h, err := parseHash(encoded)
	if err != nil {
		return false, err
	}
	got, err := pbkdf2.Key(sha256.New, password, h.salt, h.iter, len(h.key))
	if err != nil {
		return false, err
	}
	return subtle.ConstantTimeCompare(got, h.key) == 1, nil
}

// UserStore maps usernames to password hashes. It is read-only after
// construction, so it needs no locking.
type UserStore struct {
	hashes map[string]string
	// dummy is a real hash of a random password, at the same cost as the
	// stored ones. Unknown users are checked against it so "no such user"
	// takes as long as "wrong password" and usernames can't be enumerated by timing.
	dummy string
}

// NewUserStore validates every hash up front, so a typo in the users file fails
// at startup rather than as a confusing 401 later.
func NewUserStore(hashes map[string]string, iterations int) (*UserStore, error) {
	for name, h := range hashes {
		if name == "" || strings.ContainsRune(name, ':') {
			return nil, fmt.Errorf("invalid username %q", name)
		}
		if _, err := parseHash(h); err != nil {
			return nil, fmt.Errorf("user %q: %w", name, err)
		}
	}
	var random [16]byte
	if _, err := rand.Read(random[:]); err != nil {
		return nil, err
	}
	dummy, err := HashPassword(string(random[:]), iterations)
	if err != nil {
		return nil, err
	}
	copied := make(map[string]string, len(hashes))
	for k, v := range hashes {
		copied[k] = v
	}
	return &UserStore{hashes: copied, dummy: dummy}, nil
}

// Authenticate reports whether the username/password pair is valid. It always
// performs exactly one key derivation, whether or not the user exists.
func (s *UserStore) Authenticate(username, password string) bool {
	h, known := s.hashes[username]
	if !known {
		h = s.dummy
	}
	ok, err := VerifyPassword(password, h)
	return ok && err == nil && known
}

// ParseUsersFile reads "username:hash" lines. Blank lines and lines starting
// with '#' are ignored.
func ParseUsersFile(r io.Reader) (map[string]string, error) {
	users := map[string]string{}
	sc := bufio.NewScanner(r)
	for n := 1; sc.Scan(); n++ {
		line := strings.TrimSpace(sc.Text())
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}
		name, hash, ok := strings.Cut(line, ":")
		if !ok || name == "" || hash == "" {
			return nil, fmt.Errorf("line %d: want username:hash", n)
		}
		if _, dup := users[name]; dup {
			return nil, fmt.Errorf("line %d: duplicate user %q", n, name)
		}
		users[name] = hash
	}
	return users, sc.Err()
}
