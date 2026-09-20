(ns tetris.core
  "Pure game logic -- board, pieces, movement, scoring.
   No rendering, no I/O, no randomness escapes this namespace's boundary
   except through random-kind, which every entry point takes as an
   explicit argument so the rest of the logic stays deterministic and
   testable without stubbing anything.")

(def rows 20)
(def cols 10)

;; Each piece is a set of 4 rotation states; each state is 4 [row col]
;; cells inside a small bounding box. No wall-kick table -- a rotation
;; that doesn't fit is simply rejected (see LEARNING.md for why that
;; scope cut is fine for this project).
(def piece-defs
  {:I {:color "#29B6F6"
       :rotations [[[1 0] [1 1] [1 2] [1 3]]
                   [[0 2] [1 2] [2 2] [3 2]]
                   [[2 0] [2 1] [2 2] [2 3]]
                   [[0 1] [1 1] [2 1] [3 1]]]}
   :O {:color "#FDD835"
       :rotations (vec (repeat 4 [[0 1] [0 2] [1 1] [1 2]]))}
   :T {:color "#AB47BC"
       :rotations [[[0 1] [1 0] [1 1] [1 2]]
                   [[0 1] [1 1] [1 2] [2 1]]
                   [[1 0] [1 1] [1 2] [2 1]]
                   [[0 1] [1 0] [1 1] [2 1]]]}
   :S {:color "#66BB6A"
       :rotations [[[0 1] [0 2] [1 0] [1 1]]
                   [[0 1] [1 1] [1 2] [2 2]]
                   [[1 1] [1 2] [2 0] [2 1]]
                   [[0 0] [1 0] [1 1] [2 1]]]}
   :Z {:color "#EF5350"
       :rotations [[[0 0] [0 1] [1 1] [1 2]]
                   [[0 2] [1 1] [1 2] [2 1]]
                   [[1 0] [1 1] [2 1] [2 2]]
                   [[0 1] [1 0] [1 1] [2 0]]]}
   :J {:color "#42A5F5"
       :rotations [[[0 0] [1 0] [1 1] [1 2]]
                   [[0 1] [0 2] [1 1] [2 1]]
                   [[1 0] [1 1] [1 2] [2 2]]
                   [[0 1] [1 1] [2 0] [2 1]]]}
   :L {:color "#FFA726"
       :rotations [[[0 2] [1 0] [1 1] [1 2]]
                   [[0 1] [1 1] [2 1] [2 2]]
                   [[1 0] [1 1] [1 2] [2 0]]
                   [[0 0] [0 1] [1 1] [2 1]]]}})

(def piece-kinds (vec (keys piece-defs)))

(defn random-kind
  "The one place randomness enters the game. Callers that need
   determinism (tests, replays) never call this -- they build pieces
   and state with explicit kinds instead."
  []
  (rand-nth piece-kinds))

(defn empty-board []
  (vec (repeat rows (vec (repeat cols nil)))))

(defn cells-for [kind rotation]
  (get-in piece-defs [kind :rotations (mod rotation 4)]))

(defn absolute-cells
  "The piece's cells in board coordinates."
  [{:keys [kind rotation row col]}]
  (map (fn [[r c]] [(+ r row) (+ c col)]) (cells-for kind rotation)))

(defn in-bounds? [[r c]]
  (and (>= r 0) (< r rows) (>= c 0) (< c cols)))

(defn cell-free? [board [r c]]
  (nil? (get-in board [r c])))

(defn valid-position? [board piece]
  (every? (fn [cell] (and (in-bounds? cell) (cell-free? board cell)))
          (absolute-cells piece)))

(defn spawn-piece [kind]
  {:kind kind :rotation 0 :row 0 :col 3})

(defn- try-move [board piece d-row d-col]
  (let [moved (-> piece (update :row + d-row) (update :col + d-col))]
    (if (valid-position? board moved) moved piece)))

(defn move-left [board piece] (try-move board piece 0 -1))
(defn move-right [board piece] (try-move board piece 0 1))

(defn try-rotate [board piece]
  (let [rotated (update piece :rotation inc)]
    (if (valid-position? board rotated) rotated piece)))

(defn soft-drop
  "One gravity step. Returns the piece one row down, or the same piece
   with :locked? true if it can't fall any further."
  [board piece]
  (let [moved (update piece :row inc)]
    (if (valid-position? board moved)
      {:piece moved :locked? false}
      {:piece piece :locked? true})))

(defn hard-drop [board piece]
  (loop [p piece]
    (let [moved (update p :row inc)]
      (if (valid-position? board moved)
        (recur moved)
        p))))

(defn lock-piece
  "Bakes a piece's cells into the board as filled color cells."
  [board piece]
  (reduce (fn [b [r c]]
            (if (in-bounds? [r c]) (assoc-in b [r c] (get-in piece-defs [(:kind piece) :color])) b))
          board
          (absolute-cells piece)))

(defn- row-full? [row] (every? some? row))

(defn clear-lines
  "Removes every full row, keeps the rest in order, and refills the top
   with fresh empty rows so the board stays rows x cols."
  [board]
  (let [kept (vec (remove row-full? board))
        cleared (- rows (count kept))
        blank-rows (vec (repeat cleared (vec (repeat cols nil))))]
    {:board (into blank-rows kept) :cleared cleared}))

(defn score-for [cleared level]
  (* level (get {0 0, 1 100, 2 300, 3 500, 4 800} cleared 800)))

(defn level-for [lines] (inc (quot lines 10)))

(defn new-game
  "kind and next-kind are passed in so the caller controls randomness;
   ui.cljs calls this with (random-kind) (random-kind), tests pass fixed
   kinds."
  [kind next-kind]
  {:board (empty-board)
   :current (spawn-piece kind)
   :next-kind next-kind
   :score 0
   :lines 0
   :level 1
   :game-over false})

(defn- lock-and-advance [state next-kind]
  (let [locked-board (lock-piece (:board state) (:current state))
        {:keys [board cleared]} (clear-lines locked-board)
        lines (+ (:lines state) cleared)
        level (level-for lines)
        score (+ (:score state) (score-for cleared level))
        spawned (spawn-piece (:next-kind state))
        game-over? (not (valid-position? board spawned))]
    (assoc state
           :board board
           :lines lines
           :level level
           :score score
           :current spawned
           :next-kind next-kind
           :game-over game-over?)))

(defn tick
  "One gravity step. next-kind is only consumed if this step locks the
   current piece and starts a new one."
  [state next-kind]
  (if (:game-over state)
    state
    (let [{:keys [piece locked?]} (soft-drop (:board state) (:current state))]
      (if locked?
        (lock-and-advance state next-kind)
        (assoc state :current piece)))))

(defn move [state dir]
  (if (:game-over state)
    state
    (assoc state :current
           (case dir
             :left (move-left (:board state) (:current state))
             :right (move-right (:board state) (:current state))
             (:current state)))))

(defn rotate [state]
  (if (:game-over state)
    state
    (assoc state :current (try-rotate (:board state) (:current state)))))

(defn drop-hard [state next-kind]
  (if (:game-over state)
    state
    (-> state
        (assoc :current (hard-drop (:board state) (:current state)))
        (lock-and-advance next-kind))))
