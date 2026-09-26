package api

import (
	"errors"
	"net/http"
	"strconv"

	"github.com/gin-gonic/gin"

	"bookstore/internal/store"
)

// BookInput is the request-body shape. It's separate from store.Book so the
// binding tags (validation) live at the API boundary, not on the domain type.
type BookInput struct {
	ISBN     string  `json:"isbn" binding:"required,isbn"`
	Title    string  `json:"title" binding:"required"`
	Author   string  `json:"author" binding:"required"`
	Price    float64 `json:"price" binding:"required,gt=0"`
	Quantity int     `json:"quantity" binding:"gte=0"`
}

func (in BookInput) toBook() store.Book {
	return store.Book{
		ISBN:     in.ISBN,
		Title:    in.Title,
		Author:   in.Author,
		Price:    in.Price,
		Quantity: in.Quantity,
	}
}

type Handler struct {
	store *store.Store
}

func NewHandler(s *store.Store) *Handler {
	return &Handler{store: s}
}

func (h *Handler) Health(c *gin.Context) {
	c.JSON(http.StatusOK, gin.H{"status": "ok"})
}

func (h *Handler) List(c *gin.Context) {
	c.JSON(http.StatusOK, h.store.List())
}

func (h *Handler) Get(c *gin.Context) {
	id, ok := parseID(c)
	if !ok {
		return
	}

	b, err := h.store.Get(id)
	if err != nil {
		respondStoreError(c, err)
		return
	}
	c.JSON(http.StatusOK, b)
}

func (h *Handler) Create(c *gin.Context) {
	var in BookInput
	if err := c.ShouldBindJSON(&in); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": err.Error()})
		return
	}

	created, err := h.store.Create(in.toBook())
	if err != nil {
		respondStoreError(c, err)
		return
	}
	c.JSON(http.StatusCreated, created)
}

func (h *Handler) Update(c *gin.Context) {
	id, ok := parseID(c)
	if !ok {
		return
	}

	var in BookInput
	if err := c.ShouldBindJSON(&in); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": err.Error()})
		return
	}

	updated, err := h.store.Update(id, in.toBook())
	if err != nil {
		respondStoreError(c, err)
		return
	}
	c.JSON(http.StatusOK, updated)
}

func (h *Handler) Delete(c *gin.Context) {
	id, ok := parseID(c)
	if !ok {
		return
	}

	if err := h.store.Delete(id); err != nil {
		respondStoreError(c, err)
		return
	}
	c.Status(http.StatusNoContent)
}

// parseID reads the ":id" path param, writing the 400 response itself on
// failure so every handler above can just check the bool and return.
func parseID(c *gin.Context) (int, bool) {
	id, err := strconv.Atoi(c.Param("id"))
	if err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": "id must be an integer"})
		return 0, false
	}
	return id, true
}

func respondStoreError(c *gin.Context, err error) {
	switch {
	case errors.Is(err, store.ErrNotFound):
		c.JSON(http.StatusNotFound, gin.H{"error": err.Error()})
	case errors.Is(err, store.ErrDuplicateISBN):
		c.JSON(http.StatusConflict, gin.H{"error": err.Error()})
	default:
		c.JSON(http.StatusInternalServerError, gin.H{"error": "internal error"})
	}
}
