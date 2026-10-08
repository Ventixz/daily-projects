package main

import (
	"fmt"
	"math/rand"
	"os"
	"os/exec"
	"time"
)

// stty puts the terminal in/out of raw-ish mode without needing x/term.
func stty(args ...string) {
	c := exec.Command("stty", args...)
	c.Stdin = os.Stdin
	_ = c.Run()
}

func keyToDir(b byte) (Dir, bool) {
	switch b {
	case 'w', 'k':
		return Up, true
	case 's', 'j':
		return Down, true
	case 'a', 'h':
		return Left, true
	case 'd', 'l':
		return Right, true
	}
	return Dir{}, false
}

func main() {
	g := NewGame(30, 15, rand.New(rand.NewSource(time.Now().UnixNano())))
	stty("-icanon", "-echo")
	defer stty("icanon", "echo")

	keys := make(chan byte)
	go func() {
		buf := make([]byte, 1)
		for {
			if _, err := os.Stdin.Read(buf); err != nil {
				close(keys)
				return
			}
			keys <- buf[0]
		}
	}()

	tick := time.NewTicker(120 * time.Millisecond)
	defer tick.Stop()
	for !g.Over {
		fmt.Print("\x1b[H\x1b[2J", g.Render(), fmt.Sprintf("score %d   wasd/hjkl move, q quit\n", g.Score))
		select {
		case k, ok := <-keys:
			if !ok || k == 'q' {
				return
			}
			if d, ok := keyToDir(k); ok {
				g.Turn(d)
			}
		case <-tick.C:
			g.Step()
		}
	}
	fmt.Print("\x1b[H\x1b[2J", g.Render())
	if g.Won {
		fmt.Println("You filled the board! Score:", g.Score)
	} else {
		fmt.Println("Game over. Score:", g.Score)
	}
}
