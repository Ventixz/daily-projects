package main

import "math/rand"

// Point is a cell on the board; X grows right, Y grows down.
type Point struct{ X, Y int }

// Dir is a unit step.
type Dir Point

var (
	Up    = Dir{0, -1}
	Down  = Dir{0, 1}
	Left  = Dir{-1, 0}
	Right = Dir{1, 0}
)

// Opposite reports whether b reverses a.
func (a Dir) Opposite(b Dir) bool { return a.X == -b.X && a.Y == -b.Y }

// Game holds all state. Body[0] is the head. Randomness is injected so tests are deterministic.
type Game struct {
	W, H  int
	Body  []Point
	Dir   Dir
	next  Dir // direction queued for the next tick
	Food  Point
	Score int
	Over  bool
	Won   bool
	rng   *rand.Rand
}

func NewGame(w, h int, rng *rand.Rand) *Game {
	g := &Game{W: w, H: h, Dir: Right, next: Right, rng: rng}
	g.Body = []Point{{w / 2, h / 2}, {w/2 - 1, h / 2}, {w/2 - 2, h / 2}}
	g.placeFood()
	return g
}

// Turn queues a direction. Reversing into the neck is ignored, judged against the
// direction actually travelled last tick, so two fast key presses can't fold the snake on itself.
func (g *Game) Turn(d Dir) {
	if !g.Dir.Opposite(d) {
		g.next = d
	}
}

// Step advances one tick.
func (g *Game) Step() {
	if g.Over {
		return
	}
	g.Dir = g.next
	head := Point{g.Body[0].X + g.Dir.X, g.Body[0].Y + g.Dir.Y}
	if head.X < 0 || head.Y < 0 || head.X >= g.W || head.Y >= g.H {
		g.Over = true
		return
	}
	eating := head == g.Food
	// The tail cell is vacated this tick unless we grow, so moving into it is legal.
	check := g.Body
	if !eating {
		check = g.Body[:len(g.Body)-1]
	}
	for _, p := range check {
		if p == head {
			g.Over = true
			return
		}
	}
	g.Body = append([]Point{head}, g.Body...)
	if eating {
		g.Score++
		if len(g.Body) == g.W*g.H {
			g.Over, g.Won = true, true
			return
		}
		g.placeFood()
	} else {
		g.Body = g.Body[:len(g.Body)-1]
	}
}

// placeFood picks uniformly among free cells (not by retrying random cells, which
// degrades as the board fills).
func (g *Game) placeFood() {
	occ := make(map[Point]bool, len(g.Body))
	for _, p := range g.Body {
		occ[p] = true
	}
	free := make([]Point, 0, g.W*g.H-len(g.Body))
	for y := 0; y < g.H; y++ {
		for x := 0; x < g.W; x++ {
			if p := (Point{x, y}); !occ[p] {
				free = append(free, p)
			}
		}
	}
	g.Food = free[g.rng.Intn(len(free))]
}

// Render draws the board with a border.
func (g *Game) Render() string {
	cells := make([][]byte, g.H)
	for y := range cells {
		cells[y] = make([]byte, g.W)
		for x := range cells[y] {
			cells[y][x] = ' '
		}
	}
	cells[g.Food.Y][g.Food.X] = '*'
	for i, p := range g.Body {
		c := byte('o')
		if i == 0 {
			c = '@'
		}
		cells[p.Y][p.X] = c
	}
	border := "+" + string(repeat('-', g.W)) + "+\n"
	out := border
	for _, row := range cells {
		out += "|" + string(row) + "|\n"
	}
	return out + border
}

func repeat(c byte, n int) []byte {
	b := make([]byte, n)
	for i := range b {
		b[i] = c
	}
	return b
}
