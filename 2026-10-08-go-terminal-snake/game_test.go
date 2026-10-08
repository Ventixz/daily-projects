package main

import (
	"math/rand"
	"strings"
	"testing"
)

func newG() *Game { return NewGame(10, 10, rand.New(rand.NewSource(1))) }

func TestMoves(t *testing.T) {
	g := newG()
	g.Food = Point{0, 0}
	h := g.Body[0]
	g.Step()
	if g.Body[0] != (Point{h.X + 1, h.Y}) || len(g.Body) != 3 {
		t.Fatalf("bad move: %v", g.Body)
	}
}

func TestReverseIgnored(t *testing.T) {
	g := newG()
	g.Turn(Left)
	g.Step()
	if g.Dir != Right {
		t.Fatal("reversed into neck")
	}
}

func TestDoubleTurnCannotFold(t *testing.T) {
	g := newG()
	g.Food = Point{0, 0}
	g.Turn(Up)
	g.Turn(Left) // Left opposes Right, still the travelled dir, so ignored
	g.Step()
	if g.Over || g.Dir != Up {
		t.Fatalf("folded: over=%v dir=%v", g.Over, g.Dir)
	}
}

func TestEatGrows(t *testing.T) {
	g := newG()
	g.Food = Point{g.Body[0].X + 1, g.Body[0].Y}
	g.Step()
	if g.Score != 1 || len(g.Body) != 4 || g.Food == g.Body[0] {
		t.Fatalf("score=%d len=%d", g.Score, len(g.Body))
	}
	for _, p := range g.Body {
		if p == g.Food {
			t.Fatal("food on snake")
		}
	}
}

func TestWallDeath(t *testing.T) {
	g := newG()
	g.Food = Point{0, 0}
	for i := 0; i < 10 && !g.Over; i++ {
		g.Step()
	}
	if !g.Over {
		t.Fatal("should hit wall")
	}
}

func TestSelfCollision(t *testing.T) {
	g := newG()
	g.Body = []Point{{5, 5}, {5, 6}, {4, 6}, {4, 5}, {4, 4}, {5, 4}, {6, 4}}
	g.Dir, g.next = Right, Right
	g.Food = Point{0, 0}
	g.Turn(Up) // head (5,5) -> (5,4) is body
	g.Step()
	if !g.Over {
		t.Fatal("expected self collision")
	}
}

func TestMovingIntoVacatedTail(t *testing.T) {
	g := newG()
	g.Body = []Point{{1, 1}, {1, 2}, {2, 2}, {2, 1}} // 2x2 loop; tail at (2,1)
	g.Dir, g.next = Down, Down
	g.Food = Point{9, 9}
	g.Turn(Right) // head -> (2,1) == tail, which vacates
	g.Step()
	if g.Over {
		t.Fatal("tail chase should be legal")
	}
}

func TestWin(t *testing.T) {
	g := NewGame(4, 1, rand.New(rand.NewSource(1)))
	g.Body = []Point{{2, 0}, {1, 0}, {0, 0}}
	g.Food = Point{3, 0}
	g.Step()
	if !g.Won || !g.Over {
		t.Fatal("full board should win")
	}
}

func TestFoodAlwaysFree(t *testing.T) {
	g := NewGame(5, 5, rand.New(rand.NewSource(7)))
	for i := 0; i < 500; i++ {
		g.placeFood()
		for _, p := range g.Body {
			if p == g.Food {
				t.Fatal("food on body")
			}
		}
	}
}

func TestRender(t *testing.T) {
	r := newG().Render()
	lines := strings.Split(strings.TrimSpace(r), "\n")
	if len(lines) != 12 || strings.Count(r, "@") != 1 || strings.Count(r, "*") != 1 || strings.Count(r, "o") != 2 {
		t.Fatalf("bad render:\n%s", r)
	}
}
