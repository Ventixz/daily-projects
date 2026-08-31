// Command gorest starts the books REST API on :8080.
package main

import (
	"log"
	"net/http"

	"gorest/internal/api"
	"gorest/internal/store"
)

func main() {
	s := store.New()
	s.Create("Dune", "Frank Herbert", 1965)
	s.Create("The Hobbit", "J.R.R. Tolkien", 1937)

	srv := api.NewServer(s)
	log.Println("listening on :8080")
	log.Fatal(http.ListenAndServe(":8080", srv))
}
