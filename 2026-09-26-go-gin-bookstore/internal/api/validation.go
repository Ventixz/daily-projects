package api

import (
	"strings"

	"github.com/gin-gonic/gin/binding"
	"github.com/go-playground/validator/v10"
)

// RegisterValidators wires custom binding tags into Gin's shared validator
// engine. It must run once before any request is bound, since the engine is
// a process-wide singleton.
func RegisterValidators() {
	v, ok := binding.Validator.Engine().(*validator.Validate)
	if !ok {
		return
	}
	_ = v.RegisterValidation("isbn", validateISBN)
}

// validateISBN accepts ISBN-10 or ISBN-13, digits only after hyphens are
// stripped. It checks shape, not the checksum digit, since the tutorial's
// point is wiring a custom validator into Gin, not implementing the full
// ISBN spec.
func validateISBN(fl validator.FieldLevel) bool {
	s := strings.ReplaceAll(fl.Field().String(), "-", "")
	if len(s) != 10 && len(s) != 13 {
		return false
	}
	for i, r := range s {
		if r >= '0' && r <= '9' {
			continue
		}
		// ISBN-10's final check digit may be 'X'.
		if r == 'X' && len(s) == 10 && i == 9 {
			continue
		}
		return false
	}
	return true
}
