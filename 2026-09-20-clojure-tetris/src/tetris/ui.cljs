(ns tetris.ui
  "Browser glue only: canvas rendering, keyboard input, the game loop.
   Every rule about what a move does lives in tetris.core -- this
   namespace just calls it and draws the result."
  (:require [tetris.core :as core]))

(def cell-size 24)

(defonce state (atom (core/new-game (core/random-kind) (core/random-kind))))
(defonce paused? (atom false))
(defonce last-tick (atom nil))

;; Exposed for the Playwright smoke test -- reading Clojure data out of a
;; live page without a REPL attached means putting a plain JS function on
;; window that snapshots the atom.
(set! (.-tetrisState js/window)
      (fn []
        (clj->js (-> (select-keys @state [:score :lines :level :game-over])
                      (assoc :current-col (:col (:current @state)))
                      (assoc :current-row (:row (:current @state)))))))

(defn- board+piece
  "The board with the currently-falling piece painted on top, for
   rendering only -- never stored back into state."
  [{:keys [board current] :as s}]
  (reduce (fn [b [r c]]
            (if (and (>= r 0) (< r core/rows) (>= c 0) (< c core/cols))
              (assoc-in b [r c] (get-in core/piece-defs [(:kind current) :color]))
              b))
          board
          (core/absolute-cells current)))

(defn- draw-cell [ctx r c color]
  (set! (.-fillStyle ctx) color)
  (.fillRect ctx (* c cell-size) (* r cell-size) (dec cell-size) (dec cell-size)))

(defn- draw-board [ctx grid]
  (set! (.-fillStyle ctx) "#111")
  (.fillRect ctx 0 0 (* core/cols cell-size) (* core/rows cell-size))
  (doseq [r (range core/rows)
          c (range core/cols)
          :let [color (get-in grid [r c])]
          :when color]
    (draw-cell ctx r c color)))

(defn- draw-next [ctx kind]
  (set! (.-fillStyle ctx) "#111")
  (.fillRect ctx 0 0 (* 4 cell-size) (* 4 cell-size))
  (doseq [[r c] (core/cells-for kind 0)]
    (draw-cell ctx r c (get-in core/piece-defs [kind :color]))))

(defn- render! []
  (let [s @state
        board-ctx (.getContext (.getElementById js/document "board") "2d")
        next-ctx (.getContext (.getElementById js/document "next") "2d")]
    (draw-board board-ctx (board+piece s))
    (draw-next next-ctx (:next-kind s))
    (set! (.-textContent (.getElementById js/document "score")) (:score s))
    (set! (.-textContent (.getElementById js/document "lines")) (:lines s))
    (set! (.-textContent (.getElementById js/document "level")) (:level s))
    (set! (.-textContent (.getElementById js/document "status"))
          (cond (:game-over s) "GAME OVER" @paused? "PAUSED" :else ""))))

(defn- gravity-interval-ms [level]
  (max 100 (- 800 (* 60 (dec level)))))

(defn- on-key [e]
  (let [code (.-code e)]
    (when (#{"ArrowLeft" "ArrowRight" "ArrowDown" "ArrowUp" "Space" "KeyP"} code)
      (.preventDefault e))
    (when-not @paused?
      (case code
        "ArrowLeft" (swap! state core/move :left)
        "ArrowRight" (swap! state core/move :right)
        "ArrowDown" (swap! state core/tick (core/random-kind))
        "ArrowUp" (swap! state core/rotate)
        "Space" (swap! state core/drop-hard (core/random-kind))
        nil))
    (when (= code "KeyP") (swap! paused? not))
    (render!)))

(defn- loop-step [now]
  (let [interval (gravity-interval-ms (:level @state))]
    (when (nil? @last-tick) (reset! last-tick now))
    (when (and (not @paused?)
               (not (:game-over @state))
               (>= (- now @last-tick) interval))
      (reset! last-tick now)
      (swap! state core/tick (core/random-kind))
      (render!)))
  (.requestAnimationFrame js/window loop-step))

(defn ^:export init []
  (.addEventListener js/document "keydown" on-key)
  (render!)
  (.requestAnimationFrame js/window loop-step))

(init)
