(ns tetris.core-test
  (:require [clojure.test :refer [deftest testing is]]
            [tetris.core :as t]))

(deftest board-shape
  (let [b (t/empty-board)]
    (is (= t/rows (count b)))
    (is (every? #(= t/cols (count %)) b))
    (is (every? nil? (apply concat b)))))

(deftest spawn-position
  (let [p (t/spawn-piece :T)]
    (is (= :T (:kind p)))
    (is (= 0 (:rotation p)))
    (is (= 0 (:row p)))
    (is (true? (t/valid-position? (t/empty-board) p)))))

(deftest every-piece-spawns-in-bounds
  (doseq [kind t/piece-kinds]
    (testing (str kind " on an empty board")
      (is (true? (t/valid-position? (t/empty-board) (t/spawn-piece kind)))))))

(deftest movement-stays-in-bounds
  (let [board (t/empty-board)
        p (t/spawn-piece :O)]
    (testing "walking left off the edge stops at the wall"
      (let [far-left (nth (iterate #(t/move-left board %) p) 20)
            leftmost-abs-col (apply min (map second (t/absolute-cells far-left)))]
        (is (= 0 leftmost-abs-col))
        (is (true? (t/valid-position? board far-left)))))
    (testing "walking right off the edge stops at the wall"
      (let [far-right (nth (iterate #(t/move-right board %) p) 20)
            rightmost-abs-col (apply max (map second (t/absolute-cells far-right)))]
        (is (= (dec t/cols) rightmost-abs-col))
        (is (true? (t/valid-position? board far-right)))))))

(deftest movement-blocked-by-stack
  ;; a wall of filled cells one column to the right of a fresh O piece
  (let [board (reduce (fn [b r] (assoc-in b [r 5] "x")) (t/empty-board) (range t/rows))
        p (t/spawn-piece :O)]
    (is (= (:col p) (:col (t/move-right board p)))
        "can't slide into an occupied column")))

(deftest o-piece-rotation-is-a-no-op
  (let [board (t/empty-board)
        p (t/spawn-piece :O)
        rotated (t/try-rotate board p)]
    (is (= (set (t/absolute-cells p)) (set (t/absolute-cells rotated))))))

(deftest rotation-rejected-when-it-would-collide
  ;; vertical I piece (rotation 1, needs only column 2) is legal; rotating
  ;; it to horizontal (rotation 2, needs the whole row) collides with a
  ;; single blocked cell in that row
  (let [board (assoc-in (t/empty-board) [2 0] "x")
        p (assoc (t/spawn-piece :I) :rotation 1 :col 0)
        rotated (t/try-rotate board p)]
    (is (true? (t/valid-position? board p)) "sanity: starting position is legal")
    (is (= (:rotation p) (:rotation rotated))
        "rotation is rejected outright, not clamped or kicked")))

(deftest soft-drop-descends-then-locks-on-the-floor
  (let [board (t/empty-board)
        p (t/spawn-piece :O)
        {p1 :piece locked1? :locked?} (t/soft-drop board p)]
    (is (false? locked1?))
    (is (= (inc (:row p)) (:row p1)))
    (let [bottomed (nth (iterate #(:piece (t/soft-drop board %)) p) 100)
          {locked? :locked?} (t/soft-drop board bottomed)]
      (is (true? locked?)))))

(deftest lock-piece-bakes-cells-into-the-board
  (let [board (t/empty-board)
        p (t/spawn-piece :O)
        locked (t/lock-piece board p)]
    (doseq [cell (t/absolute-cells p)]
      (is (some? (get-in locked cell))))
    (is (= (* t/rows t/cols) (count (apply concat (t/empty-board)))))))

(deftest clear-lines-removes-full-rows-and-keeps-board-size
  (let [full-row (vec (repeat t/cols "x"))
        board (assoc (t/empty-board) (dec t/rows) full-row (- t/rows 2) full-row)
        {:keys [board cleared]} (t/clear-lines board)]
    (is (= 2 cleared))
    (is (= t/rows (count board)))
    (is (every? nil? (apply concat (take 2 board))) "cleared rows refilled at the top")))

(deftest score-for-standard-line-values
  (is (= 0 (t/score-for 0 1)))
  (is (= 100 (t/score-for 1 1)))
  (is (= 300 (t/score-for 2 1)))
  (is (= 500 (t/score-for 3 1)))
  (is (= 800 (t/score-for 4 1)))
  (is (= 1600 (t/score-for 4 2)) "score scales with level"))

(deftest level-progresses-every-ten-lines
  (is (= 1 (t/level-for 0)))
  (is (= 1 (t/level-for 9)))
  (is (= 2 (t/level-for 10)))
  (is (= 3 (t/level-for 25))))

(deftest hard-drop-lands-on-the-floor
  (let [board (t/empty-board)
        p (t/spawn-piece :O)
        dropped (t/hard-drop board p)]
    (is (= (- t/rows 2) (:row dropped)) "O piece is 2 rows tall")
    (is (true? (t/valid-position? board dropped)))
    (is (true? (:locked? (t/soft-drop board dropped))) "resting on the floor -- next gravity step locks it")))

(deftest drop-hard-locks-updates-score-and-spawns-next
  (let [state (t/new-game :O :T)
        after (t/drop-hard state :S)]
    (is (= :T (:kind (:current after))) "the previously-queued piece is now current")
    (is (= :S (:next-kind after)))
    (is (= 0 (:score after)) "no lines cleared yet")
    (is (false? (:game-over after)))))

(deftest tick-only-locks-when-the-piece-cannot-fall-further
  (let [state (t/new-game :O :T)
        one-step (t/tick state :S)]
    (is (= :O (:kind (:current one-step))) "still the same piece, just lower")
    (is (= (inc (:row (:current state))) (:row (:current one-step))))))

(deftest game-over-when-the-next-spawn-has-nowhere-to-go
  ;; fill every row except the very top few so the next spawn collides immediately
  (let [jammed (reduce (fn [b r] (assoc-in b [r 4] "x")) (t/empty-board) (range 1 t/rows))
        state (assoc (t/new-game :O :O) :board jammed :current (t/spawn-piece :O))
        after (t/drop-hard state :O)]
    (is (true? (:game-over after)))))

(deftest move-and-rotate-are-no-ops-after-game-over
  (let [state (assoc (t/new-game :O :T) :game-over true)]
    (is (= state (t/move state :left)))
    (is (= state (t/rotate state)))
    (is (= state (t/tick state :S)))))
