package shortener

import (
	"crypto/rand"
	"math/big"
)

const alphabet = "23456789abcdefghijkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ"

// generateCode returns a random base-N code of length n, drawn from an
// alphabet that drops visually confusable characters (0/O, 1/l/I).
func generateCode(n int) (string, error) {
	code := make([]byte, n)
	max := big.NewInt(int64(len(alphabet)))
	for i := range code {
		idx, err := rand.Int(rand.Reader, max)
		if err != nil {
			return "", err
		}
		code[i] = alphabet[idx.Int64()]
	}
	return string(code), nil
}
