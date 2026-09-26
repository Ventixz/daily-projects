package api

import (
	"github.com/gin-gonic/gin"

	"bookstore/internal/store"
)

// NewRouter wires the full route tree. apiKey guards the mutating book
// routes; reads and /healthz stay open.
func NewRouter(s *store.Store, apiKey string) *gin.Engine {
	RegisterValidators()

	r := gin.New()
	r.Use(gin.Recovery(), RequestLogger())

	h := NewHandler(s)

	v1 := r.Group("/api/v1")
	v1.GET("/healthz", h.Health)

	books := v1.Group("/books")
	books.GET("", h.List)
	books.GET("/:id", h.Get)

	protected := books.Group("")
	protected.Use(RequireAPIKey(apiKey))
	protected.POST("", h.Create)
	protected.PUT("/:id", h.Update)
	protected.DELETE("/:id", h.Delete)

	return r
}
