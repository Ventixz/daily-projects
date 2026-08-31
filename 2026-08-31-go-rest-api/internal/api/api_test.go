package api

import (
	"bytes"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"

	"gorest/internal/store"
)

func newTestServer() http.Handler {
	return NewServer(store.New())
}

func doJSON(t *testing.T, srv http.Handler, method, path, body string, headers map[string]string) *httptest.ResponseRecorder {
	t.Helper()
	var r *http.Request
	if body != "" {
		r = httptest.NewRequest(method, path, strings.NewReader(body))
	} else {
		r = httptest.NewRequest(method, path, nil)
	}
	for k, v := range headers {
		r.Header.Set(k, v)
	}
	w := httptest.NewRecorder()
	srv.ServeHTTP(w, r)
	return w
}

func decodeBook(t *testing.T, w *httptest.ResponseRecorder) store.Book {
	t.Helper()
	var b store.Book
	if err := json.Unmarshal(w.Body.Bytes(), &b); err != nil {
		t.Fatalf("decoding response body %q: %v", w.Body.String(), err)
	}
	return b
}

func TestCreateBook(t *testing.T) {
	srv := newTestServer()

	w := doJSON(t, srv, "POST", "/books", `{"title":"Dune","author":"Frank Herbert","year":1965}`, nil)
	if w.Code != http.StatusCreated {
		t.Fatalf("want 201, got %d: %s", w.Code, w.Body)
	}
	if w.Header().Get("Location") != "/books/1" {
		t.Fatalf("want Location: /books/1, got %q", w.Header().Get("Location"))
	}
	if w.Header().Get("ETag") != `"v1"` {
		t.Fatalf("want ETag v1, got %q", w.Header().Get("ETag"))
	}
	b := decodeBook(t, w)
	if b.Title != "Dune" || b.ID != 1 {
		t.Fatalf("unexpected body: %+v", b)
	}
}

func TestCreateBookValidation(t *testing.T) {
	srv := newTestServer()

	w := doJSON(t, srv, "POST", "/books", `{"title":"","author":"Frank Herbert"}`, nil)
	if w.Code != http.StatusBadRequest {
		t.Fatalf("want 400 for missing title, got %d", w.Code)
	}

	w = doJSON(t, srv, "POST", "/books", `not json`, nil)
	if w.Code != http.StatusBadRequest {
		t.Fatalf("want 400 for malformed JSON, got %d", w.Code)
	}
}

func TestGetBook(t *testing.T) {
	srv := newTestServer()
	doJSON(t, srv, "POST", "/books", `{"title":"Dune","author":"Frank Herbert","year":1965}`, nil)

	w := doJSON(t, srv, "GET", "/books/1", "", nil)
	if w.Code != http.StatusOK {
		t.Fatalf("want 200, got %d", w.Code)
	}
	if w.Header().Get("ETag") != `"v1"` {
		t.Fatalf("want ETag v1, got %q", w.Header().Get("ETag"))
	}

	w = doJSON(t, srv, "GET", "/books/999", "", nil)
	if w.Code != http.StatusNotFound {
		t.Fatalf("want 404, got %d", w.Code)
	}

	w = doJSON(t, srv, "GET", "/books/not-a-number", "", nil)
	if w.Code != http.StatusBadRequest {
		t.Fatalf("want 400 for non-integer id, got %d", w.Code)
	}
}

func TestListBooksFilterAndPagination(t *testing.T) {
	srv := newTestServer()
	doJSON(t, srv, "POST", "/books", `{"title":"Dune","author":"Frank Herbert","year":1965}`, nil)
	doJSON(t, srv, "POST", "/books", `{"title":"Children of Dune","author":"Frank Herbert","year":1976}`, nil)
	doJSON(t, srv, "POST", "/books", `{"title":"Foundation","author":"Isaac Asimov","year":1951}`, nil)

	w := doJSON(t, srv, "GET", "/books", "", nil)
	if w.Header().Get("X-Total-Count") != "3" {
		t.Fatalf("want X-Total-Count 3, got %q", w.Header().Get("X-Total-Count"))
	}

	w = doJSON(t, srv, "GET", "/books?author=herbert", "", nil)
	var books []store.Book
	json.Unmarshal(w.Body.Bytes(), &books)
	if len(books) != 2 {
		t.Fatalf("want 2 Herbert books, got %d", len(books))
	}

	w = doJSON(t, srv, "GET", "/books?limit=1&offset=1", "", nil)
	json.Unmarshal(w.Body.Bytes(), &books)
	if len(books) != 1 || books[0].Title != "Children of Dune" {
		t.Fatalf("want page 2 to be 'Children of Dune', got %+v", books)
	}
	if w.Header().Get("X-Total-Count") != "3" {
		t.Fatalf("want total count to ignore pagination, got %q", w.Header().Get("X-Total-Count"))
	}

	w = doJSON(t, srv, "GET", "/books?limit=abc", "", nil)
	if w.Code != http.StatusBadRequest {
		t.Fatalf("want 400 for non-integer limit, got %d", w.Code)
	}
}

func TestUpdateRequiresIfMatch(t *testing.T) {
	srv := newTestServer()
	doJSON(t, srv, "POST", "/books", `{"title":"Dune","author":"Frank Herbert","year":1965}`, nil)

	body := `{"title":"Dune","author":"F. Herbert","year":1965}`

	w := doJSON(t, srv, "PUT", "/books/1", body, nil)
	if w.Code != http.StatusPreconditionRequired {
		t.Fatalf("want 428 with no If-Match, got %d: %s", w.Code, w.Body)
	}

	w = doJSON(t, srv, "PUT", "/books/1", body, map[string]string{"If-Match": `"v99"`})
	if w.Code != http.StatusPreconditionFailed {
		t.Fatalf("want 412 with stale If-Match, got %d: %s", w.Code, w.Body)
	}

	w = doJSON(t, srv, "PUT", "/books/1", body, map[string]string{"If-Match": `"v1"`})
	if w.Code != http.StatusOK {
		t.Fatalf("want 200 with correct If-Match, got %d: %s", w.Code, w.Body)
	}
	updated := decodeBook(t, w)
	if updated.Author != "F. Herbert" || updated.Version != 2 {
		t.Fatalf("unexpected updated book: %+v", updated)
	}
	if w.Header().Get("ETag") != `"v2"` {
		t.Fatalf("want ETag v2 after update, got %q", w.Header().Get("ETag"))
	}

	// The ETag from before the update is now stale.
	w = doJSON(t, srv, "PUT", "/books/1", body, map[string]string{"If-Match": `"v1"`})
	if w.Code != http.StatusPreconditionFailed {
		t.Fatalf("want the old ETag to be rejected after an update, got %d", w.Code)
	}
}

func TestUpdateNotFound(t *testing.T) {
	srv := newTestServer()
	w := doJSON(t, srv, "PUT", "/books/999", `{"title":"x","author":"y","year":2000}`, map[string]string{"If-Match": `"v1"`})
	if w.Code != http.StatusNotFound {
		t.Fatalf("want 404, got %d", w.Code)
	}
}

func TestDeleteBook(t *testing.T) {
	srv := newTestServer()
	doJSON(t, srv, "POST", "/books", `{"title":"Dune","author":"Frank Herbert","year":1965}`, nil)

	w := doJSON(t, srv, "DELETE", "/books/1", "", nil)
	if w.Code != http.StatusNoContent {
		t.Fatalf("want 204, got %d", w.Code)
	}

	w = doJSON(t, srv, "GET", "/books/1", "", nil)
	if w.Code != http.StatusNotFound {
		t.Fatalf("want deleted book to 404, got %d", w.Code)
	}

	w = doJSON(t, srv, "DELETE", "/books/1", "", nil)
	if w.Code != http.StatusNotFound {
		t.Fatalf("want deleting twice to 404, got %d", w.Code)
	}
}

func TestRecoverMiddlewareTurnsPanicIntoJSON500(t *testing.T) {
	panicky := http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		panic("boom")
	})
	srv := recoverMiddleware(loggingMiddleware(panicky))

	w := httptest.NewRecorder()
	r := httptest.NewRequest("GET", "/anything", nil)
	srv.ServeHTTP(w, r)

	if w.Code != http.StatusInternalServerError {
		t.Fatalf("want 500 after a panic, got %d", w.Code)
	}
	if !bytes.Contains(w.Body.Bytes(), []byte(`"error"`)) {
		t.Fatalf("want a JSON error body, got %q", w.Body.String())
	}
}

func TestUnknownRouteIs404(t *testing.T) {
	srv := newTestServer()
	w := doJSON(t, srv, "GET", "/nope", "", nil)
	if w.Code != http.StatusNotFound {
		t.Fatalf("want 404 for unknown route, got %d", w.Code)
	}
}
