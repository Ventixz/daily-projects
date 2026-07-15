// Package shortener implements the core logic of a URL shortener: minting
// short codes, mapping them back to their original URLs, counting clicks,
// and persisting the whole thing to a JSON file.
package shortener

import (
	"encoding/json"
	"errors"
	"fmt"
	"net/url"
	"os"
	"path/filepath"
	"sync"
	"time"
)

// codeLength is short enough to read out loud, long enough that a
// 56-character alphabet makes collisions rare (56^6 ≈ 3*10^10 combinations).
const codeLength = 6

const maxGenerateAttempts = 10

var (
	ErrInvalidURL   = errors.New("url must be an absolute http(s) URL")
	ErrCodeTaken    = errors.New("short code already in use")
	ErrCodeNotFound = errors.New("short code not found")
)

// Link is one shortened URL and its usage stats.
type Link struct {
	Code      string    `json:"code"`
	URL       string    `json:"url"`
	CreatedAt time.Time `json:"created_at"`
	Clicks    int       `json:"clicks"`
}

// Store holds every Link in memory, guarded by a mutex so concurrent HTTP
// handlers can read and write it safely, and mirrors it to a JSON file on
// disk so links survive a restart.
type Store struct {
	mu    sync.RWMutex
	links map[string]*Link
	path  string
}

// NewStore loads links from path if it exists, or starts empty otherwise.
func NewStore(path string) (*Store, error) {
	s := &Store{links: make(map[string]*Link), path: path}

	data, err := os.ReadFile(path)
	if errors.Is(err, os.ErrNotExist) {
		return s, nil
	}
	if err != nil {
		return nil, fmt.Errorf("reading store file: %w", err)
	}
	if len(data) == 0 {
		return s, nil
	}

	var links []*Link
	if err := json.Unmarshal(data, &links); err != nil {
		return nil, fmt.Errorf("parsing store file: %w", err)
	}
	for _, l := range links {
		s.links[l.Code] = l
	}
	return s, nil
}

// validateURL requires an absolute http(s) URL so the server never redirects
// to something like "javascript:alert(1)" or a bare "example.com" that
// browsers would treat as a relative path.
func validateURL(raw string) error {
	u, err := url.ParseRequestURI(raw)
	if err != nil {
		return ErrInvalidURL
	}
	if u.Scheme != "http" && u.Scheme != "https" {
		return ErrInvalidURL
	}
	if u.Host == "" {
		return ErrInvalidURL
	}
	return nil
}

// Create mints a new short code for rawURL and stores it. If custom is
// non-empty it is used as the code instead of a random one.
func (s *Store) Create(rawURL, custom string) (*Link, error) {
	if err := validateURL(rawURL); err != nil {
		return nil, err
	}

	s.mu.Lock()
	defer s.mu.Unlock()

	code := custom
	if code == "" {
		var err error
		code, err = s.freeCodeLocked()
		if err != nil {
			return nil, err
		}
	} else if _, taken := s.links[code]; taken {
		return nil, ErrCodeTaken
	}

	link := &Link{Code: code, URL: rawURL, CreatedAt: time.Now().UTC()}
	s.links[code] = link
	if err := s.saveLocked(); err != nil {
		return nil, err
	}
	return link, nil
}

// freeCodeLocked generates random codes until one isn't already taken.
// Must be called with s.mu held.
func (s *Store) freeCodeLocked() (string, error) {
	for i := 0; i < maxGenerateAttempts; i++ {
		code, err := generateCode(codeLength)
		if err != nil {
			return "", err
		}
		if _, taken := s.links[code]; !taken {
			return code, nil
		}
	}
	return "", fmt.Errorf("could not find a free code after %d attempts", maxGenerateAttempts)
}

// Get looks up a link by code without recording a click.
func (s *Store) Get(code string) (*Link, bool) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	l, ok := s.links[code]
	if !ok {
		return nil, false
	}
	cp := *l
	return &cp, true
}

// RecordClick increments the click count for code and persists it.
func (s *Store) RecordClick(code string) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	l, ok := s.links[code]
	if !ok {
		return ErrCodeNotFound
	}
	l.Clicks++
	return s.saveLocked()
}

// All returns every link, most recently created first.
func (s *Store) All() []*Link {
	s.mu.RLock()
	defer s.mu.RUnlock()
	out := make([]*Link, 0, len(s.links))
	for _, l := range s.links {
		cp := *l
		out = append(out, &cp)
	}
	for i := 1; i < len(out); i++ {
		for j := i; j > 0 && out[j].CreatedAt.After(out[j-1].CreatedAt); j-- {
			out[j], out[j-1] = out[j-1], out[j]
		}
	}
	return out
}

// saveLocked writes the store to disk atomically (write to a temp file, then
// rename) so a crash mid-write can't corrupt the data file. Must be called
// with s.mu held.
func (s *Store) saveLocked() error {
	if s.path == "" {
		return nil
	}
	links := make([]*Link, 0, len(s.links))
	for _, l := range s.links {
		links = append(links, l)
	}
	data, err := json.MarshalIndent(links, "", "  ")
	if err != nil {
		return err
	}

	dir := filepath.Dir(s.path)
	if dir != "." {
		if err := os.MkdirAll(dir, 0o755); err != nil {
			return err
		}
	}

	tmp := s.path + ".tmp"
	if err := os.WriteFile(tmp, data, 0o644); err != nil {
		return err
	}
	return os.Rename(tmp, s.path)
}
