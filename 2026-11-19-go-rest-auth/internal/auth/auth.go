// Package auth implements HTTP Basic authentication against a file of
// PBKDF2-hashed passwords, as middleware for net/http.
package auth

import (
	"bufio"
	"context"
	"fmt"
	"io"
	"net/http"
	"strings"

	"restauth/internal/password"
)

// Users is a read-only set of username -> encoded password hash.
type Users struct {
	hashes map[string]string

	// dummy is a real hash, with the same cost as the stored ones, that
	// Authenticate checks when the username is unknown. Without it, "no such
	// user" returns in microseconds and "wrong password" takes ~100 ms, and that
	// difference tells an attacker which usernames exist.
	dummy string

	// verify is password.Verify; tests swap it to count calls.
	verify func(pw, encoded string) (bool, error)
}

// LoadUsers reads lines of the form "name:hash". Blank lines and lines
// starting with '#' are ignored.
func LoadUsers(r io.Reader) (*Users, error) {
	u := &Users{hashes: map[string]string{}, verify: password.Verify}
	sc := bufio.NewScanner(r)
	line := 0
	for sc.Scan() {
		line++
		text := strings.TrimSpace(sc.Text())
		if text == "" || strings.HasPrefix(text, "#") {
			continue
		}
		name, hash, ok := strings.Cut(text, ":")
		if !ok || name == "" || hash == "" {
			return nil, fmt.Errorf("users line %d: want name:hash", line)
		}
		if _, err := password.Iterations(hash); err != nil {
			return nil, fmt.Errorf("users line %d (%s): %w", line, name, err)
		}
		if _, dup := u.hashes[name]; dup {
			return nil, fmt.Errorf("users line %d: duplicate user %q", line, name)
		}
		u.hashes[name] = hash
	}
	if err := sc.Err(); err != nil {
		return nil, err
	}
	// Build the dummy now, not on the first unknown login: building it costs
	// a hash, and that one slow response would itself be a timing signal.
	iters := password.DefaultIterations
	for _, h := range u.hashes { // match the cost of a real user
		iters, _ = password.Iterations(h)
		break
	}
	dummy, err := password.Hash("not-a-real-password", iters)
	if err != nil {
		return nil, err
	}
	u.dummy = dummy
	return u, nil
}

// Len returns the number of users.
func (u *Users) Len() int { return len(u.hashes) }

// Authenticate reports whether name/pw are valid. It does the same amount of
// work whether or not the user exists.
func (u *Users) Authenticate(name, pw string) bool {
	hash, known := u.hashes[name]
	if !known {
		hash = u.dummy
	}
	ok, err := u.verify(pw, hash)
	return known && ok && err == nil
}

// Authenticator is anything that can check a username and password.
type Authenticator interface {
	Authenticate(name, pw string) bool
}

type ctxKey struct{}

// UserFromContext returns the authenticated username set by BasicAuth.
func UserFromContext(ctx context.Context) (string, bool) {
	name, ok := ctx.Value(ctxKey{}).(string)
	return name, ok
}

// WithUser returns a context carrying name as the authenticated user.
func WithUser(ctx context.Context, name string) context.Context {
	return context.WithValue(ctx, ctxKey{}, name)
}

// BasicAuth wraps next so that only requests with valid credentials reach it.
// Anything else gets 401 with a WWW-Authenticate challenge, which is what
// makes browsers and curl prompt for credentials.
func BasicAuth(users Authenticator, realm string, next http.Handler) http.Handler {
	challenge := fmt.Sprintf(`Basic realm=%q, charset="UTF-8"`, realm)
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		name, pw, ok := r.BasicAuth()
		if !ok || !users.Authenticate(name, pw) {
			w.Header().Set("WWW-Authenticate", challenge)
			http.Error(w, "unauthorized", http.StatusUnauthorized)
			return
		}
		next.ServeHTTP(w, r.WithContext(WithUser(r.Context(), name)))
	})
}

// RequireTLS rejects requests that didn't arrive over TLS. Basic auth sends
// the password with every request, base64-encoded but not encrypted, so it is
// only acceptable inside an encrypted connection.
func RequireTLS(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.TLS == nil {
			http.Error(w, "TLS required", http.StatusForbidden)
			return
		}
		next.ServeHTTP(w, r)
	})
}
