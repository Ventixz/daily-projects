package api

import (
	"bytes"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"testing"

	"github.com/gin-gonic/gin"

	"bookstore/internal/store"
)

const testAPIKey = "test-key"

func newTestRouter() *gin.Engine {
	gin.SetMode(gin.TestMode)
	return NewRouter(store.New(), testAPIKey)
}

func doJSON(r *gin.Engine, method, path string, body any, apiKey string) *httptest.ResponseRecorder {
	var buf bytes.Buffer
	if body != nil {
		_ = json.NewEncoder(&buf).Encode(body)
	}
	req := httptest.NewRequest(method, path, &buf)
	req.Header.Set("Content-Type", "application/json")
	if apiKey != "" {
		req.Header.Set("X-API-Key", apiKey)
	}
	rec := httptest.NewRecorder()
	r.ServeHTTP(rec, req)
	return rec
}

func TestHealthz(t *testing.T) {
	r := newTestRouter()
	rec := doJSON(r, http.MethodGet, "/api/v1/healthz", nil, "")
	if rec.Code != http.StatusOK {
		t.Fatalf("want 200, got %d", rec.Code)
	}
}

func TestCreateRequiresAPIKey(t *testing.T) {
	r := newTestRouter()
	book := BookInput{ISBN: "0306406152", Title: "T", Author: "A", Price: 9.99}

	rec := doJSON(r, http.MethodPost, "/api/v1/books", book, "")
	if rec.Code != http.StatusUnauthorized {
		t.Fatalf("want 401 without API key, got %d: %s", rec.Code, rec.Body.String())
	}
}

func TestCreateRejectsMalformedISBN(t *testing.T) {
	r := newTestRouter()
	book := BookInput{ISBN: "not-an-isbn", Title: "T", Author: "A", Price: 9.99}

	rec := doJSON(r, http.MethodPost, "/api/v1/books", book, testAPIKey)
	if rec.Code != http.StatusBadRequest {
		t.Fatalf("want 400 for bad ISBN, got %d: %s", rec.Code, rec.Body.String())
	}
}

func TestCreateRejectsNonPositivePrice(t *testing.T) {
	r := newTestRouter()
	book := BookInput{ISBN: "0306406152", Title: "T", Author: "A", Price: 0}

	rec := doJSON(r, http.MethodPost, "/api/v1/books", book, testAPIKey)
	if rec.Code != http.StatusBadRequest {
		t.Fatalf("want 400 for zero price, got %d: %s", rec.Code, rec.Body.String())
	}
}

func TestCreateThenGetRoundTrips(t *testing.T) {
	r := newTestRouter()
	book := BookInput{ISBN: "0306406152", Title: "Gödel, Escher, Bach", Author: "Hofstadter", Price: 24.5, Quantity: 3}

	createRec := doJSON(r, http.MethodPost, "/api/v1/books", book, testAPIKey)
	if createRec.Code != http.StatusCreated {
		t.Fatalf("want 201, got %d: %s", createRec.Code, createRec.Body.String())
	}

	var created store.Book
	if err := json.Unmarshal(createRec.Body.Bytes(), &created); err != nil {
		t.Fatalf("decode create response: %v", err)
	}
	if created.ID == 0 {
		t.Fatalf("want a non-zero assigned ID, got %+v", created)
	}

	getRec := doJSON(r, http.MethodGet, "/api/v1/books/1", nil, "")
	if getRec.Code != http.StatusOK {
		t.Fatalf("want 200, got %d: %s", getRec.Code, getRec.Body.String())
	}

	var fetched store.Book
	if err := json.Unmarshal(getRec.Body.Bytes(), &fetched); err != nil {
		t.Fatalf("decode get response: %v", err)
	}
	if fetched != created {
		t.Fatalf("get returned %+v, want %+v", fetched, created)
	}
}

func TestCreateDuplicateISBNConflicts(t *testing.T) {
	r := newTestRouter()
	book := BookInput{ISBN: "0306406152", Title: "T", Author: "A", Price: 9.99}

	doJSON(r, http.MethodPost, "/api/v1/books", book, testAPIKey)
	rec := doJSON(r, http.MethodPost, "/api/v1/books", book, testAPIKey)

	if rec.Code != http.StatusConflict {
		t.Fatalf("want 409 on duplicate ISBN, got %d: %s", rec.Code, rec.Body.String())
	}
}

func TestGetMissingReturns404(t *testing.T) {
	r := newTestRouter()
	rec := doJSON(r, http.MethodGet, "/api/v1/books/999", nil, "")
	if rec.Code != http.StatusNotFound {
		t.Fatalf("want 404, got %d: %s", rec.Code, rec.Body.String())
	}
}

func TestGetNonIntegerIDReturns400(t *testing.T) {
	r := newTestRouter()
	rec := doJSON(r, http.MethodGet, "/api/v1/books/abc", nil, "")
	if rec.Code != http.StatusBadRequest {
		t.Fatalf("want 400, got %d: %s", rec.Code, rec.Body.String())
	}
}

func TestDeleteThenGetReturns404(t *testing.T) {
	r := newTestRouter()
	book := BookInput{ISBN: "0306406152", Title: "T", Author: "A", Price: 9.99}
	doJSON(r, http.MethodPost, "/api/v1/books", book, testAPIKey)

	delRec := doJSON(r, http.MethodDelete, "/api/v1/books/1", nil, testAPIKey)
	if delRec.Code != http.StatusNoContent {
		t.Fatalf("want 204, got %d: %s", delRec.Code, delRec.Body.String())
	}

	getRec := doJSON(r, http.MethodGet, "/api/v1/books/1", nil, "")
	if getRec.Code != http.StatusNotFound {
		t.Fatalf("want 404 after delete, got %d", getRec.Code)
	}
}

func TestUpdateRequiresAPIKey(t *testing.T) {
	r := newTestRouter()
	book := BookInput{ISBN: "0306406152", Title: "T", Author: "A", Price: 9.99}
	doJSON(r, http.MethodPost, "/api/v1/books", book, testAPIKey)

	book.Price = 12.0
	rec := doJSON(r, http.MethodPut, "/api/v1/books/1", book, "")
	if rec.Code != http.StatusUnauthorized {
		t.Fatalf("want 401, got %d", rec.Code)
	}
}

func TestListReturnsAllCreatedBooks(t *testing.T) {
	r := newTestRouter()
	doJSON(r, http.MethodPost, "/api/v1/books", BookInput{ISBN: "0306406152", Title: "A", Author: "X", Price: 1}, testAPIKey)
	doJSON(r, http.MethodPost, "/api/v1/books", BookInput{ISBN: "0131103628", Title: "B", Author: "Y", Price: 2}, testAPIKey)

	rec := doJSON(r, http.MethodGet, "/api/v1/books", nil, "")
	var got []store.Book
	if err := json.Unmarshal(rec.Body.Bytes(), &got); err != nil {
		t.Fatalf("decode: %v", err)
	}
	if len(got) != 2 {
		t.Fatalf("want 2 books, got %d: %+v", len(got), got)
	}
}
