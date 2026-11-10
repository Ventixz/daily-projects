package main

import (
	"bytes"
	"io"
	"log"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"
)

func do(h http.Handler, method, path string, hdr map[string]string) *httptest.ResponseRecorder {
	r := httptest.NewRequest(method, path, nil)
	r.RemoteAddr = "1.2.3.4:5555"
	for k, v := range hdr {
		r.Header.Set(k, v)
	}
	w := httptest.NewRecorder()
	h.ServeHTTP(w, r)
	return w
}

func newTestHandler(buf *bytes.Buffer, burst int, now func() time.Time) http.Handler {
	return NewHandler(log.New(buf, "", 0), "s3cret", NewRateLimiter(1, burst, now))
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
	do(h, "GET", "/", nil)
	if strings.Join(order, ",") != "a,b,h" {
		t.Fatalf("order = %v", order)
	}
}

func TestRequestIDGeneratedAndPreserved(t *testing.T) {
	h := newTestHandler(&bytes.Buffer{}, 10, nil)
	if id := do(h, "GET", "/health", nil).Header().Get("X-Request-ID"); len(id) != 16 {
		t.Fatalf("generated id = %q", id)
	}
	w := do(h, "GET", "/health", map[string]string{"X-Request-ID": "abc"})
	if w.Header().Get("X-Request-ID") != "abc" || !strings.Contains(w.Body.String(), "abc") {
		t.Fatalf("id not preserved: %q %q", w.Header().Get("X-Request-ID"), w.Body.String())
	}
}

func TestAuth(t *testing.T) {
	h := newTestHandler(&bytes.Buffer{}, 10, nil)
	if c := do(h, "GET", "/secret", nil).Code; c != 401 {
		t.Fatalf("no token: %d", c)
	}
	if c := do(h, "GET", "/secret", map[string]string{"Authorization": "Bearer wrong"}).Code; c != 401 {
		t.Fatalf("bad token: %d", c)
	}
	w := do(h, "GET", "/secret", map[string]string{"Authorization": "Bearer s3cret"})
	if w.Code != 200 || !strings.Contains(w.Body.String(), "42") {
		t.Fatalf("good token: %d %q", w.Code, w.Body.String())
	}
}

func TestRecoverAndLogStatus(t *testing.T) {
	var buf bytes.Buffer
	h := newTestHandler(&buf, 10, nil)
	if c := do(h, "GET", "/boom", nil).Code; c != 500 {
		t.Fatalf("panic status = %d", c)
	}
	out := buf.String()
	if !strings.Contains(out, "panic: kaboom") || !strings.Contains(out, "/boom -> 500") {
		t.Fatalf("log = %q", out)
	}
}

func TestRateLimitRefill(t *testing.T) {
	clock := time.Unix(1000, 0)
	h := newTestHandler(&bytes.Buffer{}, 2, func() time.Time { return clock })
	for i := 0; i < 2; i++ {
		if c := do(h, "GET", "/health", nil).Code; c != 200 {
			t.Fatalf("req %d: %d", i, c)
		}
	}
	w := do(h, "GET", "/health", nil)
	if w.Code != 429 || w.Header().Get("Retry-After") == "" {
		t.Fatalf("expected 429, got %d", w.Code)
	}
	clock = clock.Add(1100 * time.Millisecond)
	if c := do(h, "GET", "/health", nil).Code; c != 200 {
		t.Fatalf("after refill: %d", c)
	}
}

func TestRateLimitPerClient(t *testing.T) {
	rl := NewRateLimiter(1, 1, func() time.Time { return time.Unix(0, 0) })
	if !rl.Allow("a") || rl.Allow("a") || !rl.Allow("b") {
		t.Fatal("clients must have independent buckets")
	}
}

func TestEndToEndServer(t *testing.T) {
	srv := httptest.NewServer(newTestHandler(&bytes.Buffer{}, 10, nil))
	defer srv.Close()
	resp, err := http.Get(srv.URL + "/health")
	if err != nil {
		t.Fatal(err)
	}
	b, _ := io.ReadAll(resp.Body)
	resp.Body.Close()
	if resp.StatusCode != 200 || !strings.HasPrefix(string(b), "ok ") {
		t.Fatalf("%d %q", resp.StatusCode, b)
	}
}
