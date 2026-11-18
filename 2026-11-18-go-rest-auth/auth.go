package main

import (
	"context"
	"net/http"
)

type userKey struct{}

// UserFromContext returns the authenticated username set by BasicAuth.
func UserFromContext(ctx context.Context) (string, bool) {
	u, ok := ctx.Value(userKey{}).(string)
	return u, ok
}

// BasicAuth wraps next so it only runs for requests carrying valid HTTP Basic
// credentials. Basic auth sends the password in every request (base64 is an
// encoding, not encryption), so it refuses plaintext requests outright instead
// of checking, and thereby leaking, credentials sent without TLS.
func BasicAuth(users *UserStore, realm string, next http.Handler) http.Handler {
	challenge := `Basic realm="` + realm + `", charset="UTF-8"`
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.TLS == nil {
			http.Error(w, "TLS required", http.StatusForbidden)
			return
		}
		user, pass, ok := r.BasicAuth()
		if !ok || !users.Authenticate(user, pass) {
			// The challenge header is what makes a browser show its login prompt.
			w.Header().Set("WWW-Authenticate", challenge)
			http.Error(w, "unauthorized", http.StatusUnauthorized)
			return
		}
		next.ServeHTTP(w, r.WithContext(context.WithValue(r.Context(), userKey{}, user)))
	})
}
