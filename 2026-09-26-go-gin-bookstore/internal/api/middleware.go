package api

import (
	"log"
	"net/http"
	"time"

	"github.com/gin-gonic/gin"
)

// RequestLogger logs one line per request after the handler chain runs, so
// it can report the status code the handler actually set.
func RequestLogger() gin.HandlerFunc {
	return func(c *gin.Context) {
		start := time.Now()
		path := c.Request.URL.Path

		c.Next()

		log.Printf("%s %s -> %d (%s)", c.Request.Method, path, c.Writer.Status(), time.Since(start))
	}
}

// RequireAPIKey guards the write routes. It's registered only on the
// sub-group that needs it, not globally, so reads stay open.
func RequireAPIKey(key string) gin.HandlerFunc {
	return func(c *gin.Context) {
		if c.GetHeader("X-API-Key") != key {
			c.AbortWithStatusJSON(http.StatusUnauthorized, gin.H{"error": "missing or invalid X-API-Key header"})
			return
		}
		c.Next()
	}
}
