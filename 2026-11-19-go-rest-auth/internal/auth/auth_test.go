package auth

import (
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"

	"restauth/internal/password"
)

func mustHash(t *testing.T, pw string) string {
	t.Helper()
	h, err := password.Hash(pw, 1000)
	if err != nil {
		t.Fatal(err)
	}
	return h
}

func testUsers(t *testing.T) *Users {
	t.Helper()
	file := "# comment\n\nalice:" + mustHash(t, "alice-secret") + "\nbob:" + mustHash(t, "bob-secret") + "\n"
	u, err := LoadUsers(strings.NewReader(file))
	if err != nil {
		t.Fatal(err)
	}
	return u
}

func TestLoadUsers(t *testing.T) {
	u := testUsers(t)
	if u.Len() != 2 {
		t.Fatalf("Len = %d, want 2", u.Len())
	}
	h := mustHash(t, "x")
	bad := map[string]string{
		"no colon":   "alice " + h,
		"empty name": ":" + h,
		"plaintext":  "alice:hunter2",
		"duplicate":  "alice:" + h + "\nalice:" + h,
		"empty hash": "alice:",
	}
	for name, file := range bad {
		if _, err := LoadUsers(strings.NewReader(file)); err == nil {
			t.Errorf("%s: LoadUsers accepted %q", name, file)
		}
	}
}

func TestAuthenticate(t *testing.T) {
	u := testUsers(t)
	if !u.Authenticate("alice", "alice-secret") {
		t.Error("valid credentials rejected")
	}
	if u.Authenticate("alice", "bob-secret") {
		t.Error("another user's password accepted")
	}
	if u.Authenticate("mallory", "alice-secret") {
		t.Error("unknown user accepted")
	}
	if u.Authenticate("", "") {
		t.Error("empty credentials accepted")
	}
}

// An unknown user must cost a full hash verification, the same as a wrong
// password, so timing can't be used to enumerate usernames.
func TestUnknownUserStillVerifiesAHash(t *testing.T) {
	u := testUsers(t)
	calls := 0
	var hashes []string
	u.verify = func(pw, enc string) (bool, error) {
		calls++
		hashes = append(hashes, enc)
		return password.Verify(pw, enc)
	}
	u.Authenticate("nobody", "whatever")
	u.Authenticate("alice", "wrong")
	if calls != 2 {
		t.Fatalf("verify called %d times, want 2 (once per attempt)", calls)
	}
	n0, err := password.Iterations(hashes[0])
	if err != nil {
		t.Fatalf("dummy hash is not a real hash: %v", err)
	}
	if n1, _ := password.Iterations(hashes[1]); n0 != n1 {
		t.Fatalf("dummy cost %d != real cost %d", n0, n1)
	}
	if u.Authenticate("nobody", "not-a-real-password") {
		t.Fatal("the dummy hash's own password logged in an unknown user")
	}
}

func TestBasicAuthMiddleware(t *testing.T) {
	u := testUsers(t)
	h := BasicAuth(u, "tasks", http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		name, ok := UserFromContext(r.Context())
		if !ok {
			t.Error("no user in context")
		}
		w.Write([]byte("hello " + name))
	}))

	do := func(set func(*http.Request)) *httptest.ResponseRecorder {
		r := httptest.NewRequest("GET", "/", nil)
		if set != nil {
			set(r)
		}
		w := httptest.NewRecorder()
		h.ServeHTTP(w, r)
		return w
	}

	w := do(nil)
	if w.Code != 401 {
		t.Fatalf("no credentials: status %d, want 401", w.Code)
	}
	if got := w.Header().Get("WWW-Authenticate"); got != `Basic realm="tasks", charset="UTF-8"` {
		t.Fatalf("challenge = %q", got)
	}
	if w := do(func(r *http.Request) { r.SetBasicAuth("alice", "nope") }); w.Code != 401 {
		t.Fatalf("wrong password: status %d", w.Code)
	}
	if w := do(func(r *http.Request) { r.Header.Set("Authorization", "Bearer abc") }); w.Code != 401 {
		t.Fatalf("wrong scheme: status %d", w.Code)
	}
	if w := do(func(r *http.Request) { r.Header.Set("Authorization", "Basic !!!notbase64") }); w.Code != 401 {
		t.Fatalf("garbage header: status %d", w.Code)
	}
	w = do(func(r *http.Request) { r.SetBasicAuth("bob", "bob-secret") })
	if w.Code != 200 || w.Body.String() != "hello bob" {
		t.Fatalf("valid: %d %q", w.Code, w.Body.String())
	}
}

func TestRequireTLS(t *testing.T) {
	h := RequireTLS(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {}))
	w := httptest.NewRecorder()
	h.ServeHTTP(w, httptest.NewRequest("GET", "/", nil))
	if w.Code != 403 {
		t.Fatalf("plain request: status %d, want 403", w.Code)
	}
}
