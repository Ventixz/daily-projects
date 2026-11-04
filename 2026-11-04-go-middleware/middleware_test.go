package main

import (
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"
)

func do(h http.Handler, path, auth string) *httptest.ResponseRecorder {
	req := httptest.NewRequest("GET", path, nil)
	if auth != "" {
		req.Header.Set("Authorization", auth)
	}
	rec := httptest.NewRecorder()
	h.ServeHTTP(rec, req)
	return rec
}

func TestChainOrder(t *testing.T) {
	var order []string
	mk := func(name string) Middleware {
		return func(n http.Handler) http.Handler {
			return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
				order = append(order, name)
				n.ServeHTTP(w, r)
			})
		}
	}
	h := Chain(http.HandlerFunc(func(http.ResponseWriter, *http.Request) { order = append(order, "h") }), mk("a"), mk("b"))
	do(h, "/", "")
	if strings.Join(order, ",") != "a,b,h" {
		t.Fatalf("order = %v", order)
	}
}

func TestAuth(t *testing.T) {
	h := NewServer(func(string, ...any) {})
	if c := do(h, "/hello", "").Code; c != 401 {
		t.Fatalf("no token: %d", c)
	}
	if c := do(h, "/hello", "Bearer nope").Code; c != 401 {
		t.Fatalf("bad token: %d", c)
	}
	r := do(h, "/hello", "Bearer secret")
	if r.Code != 200 || r.Body.String() != "hello\n" {
		t.Fatalf("good token: %d %q", r.Code, r.Body.String())
	}
	if do(h, "/hello", "").Header().Get("WWW-Authenticate") != "Bearer" {
		t.Fatal("missing WWW-Authenticate")
	}
}

func TestRecover(t *testing.T) {
	var logs []string
	h := NewServer(func(f string, a ...any) { logs = append(logs, f) })
	if c := do(h, "/boom", "Bearer secret").Code; c != 500 {
		t.Fatalf("code %d", c)
	}
	// server still works afterwards
	if c := do(h, "/hello", "Bearer secret").Code; c != 200 {
		t.Fatalf("after panic: %d", c)
	}
}

func TestLoggingRecordsStatus(t *testing.T) {
	var line string
	h := NewServer(func(f string, a ...any) {
		if strings.HasPrefix(f, "%s %s") {
			line = f
			_ = a
		}
	})
	do(h, "/hello", "")
	if line == "" {
		t.Fatal("nothing logged")
	}
	var got []any
	h2 := Logging(func(f string, a ...any) { got = a })(http.NotFoundHandler())
	do(h2, "/x", "")
	if got[2] != 404 {
		t.Fatalf("status logged = %v", got[2])
	}
}

func TestRateLimit(t *testing.T) {
	now := time.Unix(0, 0)
	h := Chain(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {}),
		RateLimit(2, time.Minute, func() time.Time { return now }))
	for i := 0; i < 2; i++ {
		if c := do(h, "/", "").Code; c != 200 {
			t.Fatalf("req %d: %d", i, c)
		}
	}
	r := do(h, "/", "")
	if r.Code != 429 || r.Header().Get("Retry-After") != "60" {
		t.Fatalf("limit: %d %q", r.Code, r.Header().Get("Retry-After"))
	}
	now = now.Add(61 * time.Second)
	if c := do(h, "/", "").Code; c != 200 {
		t.Fatalf("after window: %d", c)
	}
}
