//go:build js && wasm

// Command wasm exposes calc.Eval to JavaScript as globalThis.goCalc(expr).
// Build: GOOS=js GOARCH=wasm go build -o web/calc.wasm ./cmd/wasm
package main

import (
	"syscall/js"

	"gocalc/calc"
)

func main() {
	js.Global().Set("goCalc", js.FuncOf(func(this js.Value, args []js.Value) any {
		v, err := calc.Eval(args[0].String())
		if err != nil {
			return map[string]any{"error": err.Error()}
		}
		return map[string]any{"value": v}
	}))
	select {} // keep the Go runtime alive for callbacks
}
