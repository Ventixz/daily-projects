package main

import (
	"bytes"
	"log"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"
)

func get(h http.Handler, path, auth, addr string) *httptest.ResponseRecorder {
	req := httptest.NewRequest("GET", path, nil)
	req.RemoteAddr = addr
	if auth != "" {
		req.Header.Set("Authorization", auth)
	}
	rr := httptest.NewRecorder()
	h.ServeHTTP(rr, req)
	return rr
}

func newApp(buf *bytes.Buffer, clock *time.Time) http.Handler {
	return NewHandler(log.New(buf, "", 0), func() time.Time { return *clock })
}

func TestChainOrder(t *testing.T) {
	var order []string
	mk := func(n string) Middleware {
		return func(next http.Handler) http.Handler {
			return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
				order = append(order, n)
				next.ServeHTTP(w, r)
			})
		}
	}
	h := Chain(http.HandlerFunc(func(http.ResponseWriter, *http.Request) { order = append(order, "h") }), mk("a"), mk("b"))
	h.ServeHTTP(httptest.NewRecorder(), httptest.NewRequest("GET", "/", nil))
	if strings.Join(order, ",") != "a,b,h" {
		t.Fatalf("order = %v", order)
	}
}

func TestHealthHasRequestIDAndIsLogged(t *testing.T) {
	var buf bytes.Buffer
	clock := time.Unix(0, 0)
	rr := get(newApp(&buf, &clock), "/health", "", "1.2.3.4:1")
	if rr.Code != 200 || rr.Header().Get("X-Request-Id") == "" {
		t.Fatalf("code=%d id=%q", rr.Code, rr.Header().Get("X-Request-Id"))
	}
	if !strings.Contains(buf.String(), "GET /health -> 200") || !strings.Contains(buf.String(), rr.Header().Get("X-Request-Id")) {
		t.Fatalf("log = %q", buf.String())
	}
}

func TestAuth(t *testing.T) {
	var buf bytes.Buffer
	clock := time.Unix(0, 0)
	app := newApp(&buf, &clock)
	for _, a := range []string{"", "Bearer nope", "Basic s3cret", "s3cret"} {
		if rr := get(app, "/whoami", a, "1.1.1.1:1"); rr.Code != 401 || rr.Header().Get("WWW-Authenticate") == "" {
			t.Fatalf("auth %q -> %d", a, rr.Code)
		}
	}
	rr := get(app, "/whoami", "Bearer s3cret", "1.1.1.1:1")
	if rr.Code != 200 || !strings.Contains(rr.Body.String(), `"alice"`) {
		t.Fatalf("got %d %q", rr.Code, rr.Body.String())
	}
}

func TestRecoverLogsStatus500(t *testing.T) {
	var buf bytes.Buffer
	clock := time.Unix(0, 0)
	rr := get(newApp(&buf, &clock), "/boom", "", "2.2.2.2:1")
	if rr.Code != 500 || !strings.Contains(buf.String(), "panic: kaboom") {
		t.Fatalf("code=%d log=%q", rr.Code, buf.String())
	}
}

func TestRateLimitAndRefill(t *testing.T) {
	var buf bytes.Buffer
	clock := time.Unix(100, 0)
	app := newApp(&buf, &clock)
	for i := 0; i < 10; i++ {
		if rr := get(app, "/health", "", "3.3.3.3:9"); rr.Code != 200 {
			t.Fatalf("req %d -> %d", i, rr.Code)
		}
	}
	rr := get(app, "/health", "", "3.3.3.3:10") // same IP, other port
	if rr.Code != 429 || rr.Header().Get("Retry-After") == "" {
		t.Fatalf("expected 429, got %d", rr.Code)
	}
	if get(app, "/health", "", "4.4.4.4:1").Code != 200 {
		t.Fatal("other client should be unaffected")
	}
	clock = clock.Add(400 * time.Millisecond) // 2 tokens at 5/s
	if get(app, "/health", "", "3.3.3.3:9").Code != 200 {
		t.Fatal("should refill")
	}
}
