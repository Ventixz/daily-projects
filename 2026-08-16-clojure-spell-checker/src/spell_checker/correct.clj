(ns spell-checker.correct
  "The corrector itself: Peter Norvig's 'How to Write a Spelling
   Corrector' (https://norvig.com/spell-correct.html), translated from his
   21-line Python into Clojure rather than ported line-for-line."
  (:require [clojure.string :as str]))

(def alphabet "abcdefghijklmnopqrstuvwxyz")

(defn- splits
  "All (left, right) ways of cutting `word` in two, including the empty
   splits at each end."
  [word]
  (for [i (range (inc (count word)))]
    [(subs word 0 i) (subs word i)]))

(defn edits1
  "Every word one edit away from `word`: a single deletion, adjacent-letter
   transposition, letter replacement, or letter insertion."
  [word]
  (let [sp (splits word)
        deletes    (for [[l r] sp :when (seq r)]
                     (str l (subs r 1)))
        transposes (for [[l r] sp :when (> (count r) 1)]
                     (str l (subs r 1 2) (subs r 0 1) (subs r 2)))
        replaces   (for [[l r] sp :when (seq r), c alphabet]
                     (str l c (subs r 1)))
        inserts    (for [[l r] sp, c alphabet]
                     (str l c r))]
    (set (concat deletes transposes replaces inserts))))

(defn edits2
  "Every word reachable from `word` by two edits, by running edits1 on
   every result of edits1. There's no filtering against the dictionary
   here on purpose -- `known` does that -- so this set is large (tens of
   thousands of entries even for a short word) and is only ever consumed
   through `known`."
  [word]
  (set (mapcat edits1 (edits1 word))))

(defn known
  "The subset of `words` that actually appear in `model`."
  [model words]
  (set (filter #(contains? model %) words)))

(defn candidates
  "The best-available pool of correction candidates for `word`: the word
   itself if it's already known, else known 1-edit neighbors, else known
   2-edit neighbors, else -- nothing in the dictionary is close -- the
   word unchanged."
  [model word]
  (or (not-empty (known model [word]))
      (not-empty (known model (edits1 word)))
      (not-empty (known model (edits2 word)))
      #{word}))

(defn correction
  "The most frequent candidate for `word` in the corpus `model`, used as
   a stand-in for `argmax P(candidate | word)` -- Norvig's actual formula
   discounts each candidate by edit distance too, but on his corpus the
   raw-frequency approximation gets the same answer often enough to be
   worth the simplicity."
  [model word]
  (apply max-key #(get model % 0) (candidates model word)))

(defn- case-of
  "Which string-case function turned `text` into its current case, so a
   correction can be written back in the same case it was asked in."
  [text]
  (cond
    (= text (str/upper-case text))     str/upper-case
    (= text (str/lower-case text))     str/lower-case
    (= text (str/capitalize text))     str/capitalize
    :else                              identity))

(defn correct-word
  "Correct a single token, preserving its original case."
  [model word]
  ((case-of word) (correction model (str/lower-case word))))

(defn correct-text
  "Correct every alphabetic run in `text` in place, leaving punctuation,
   whitespace, and digits untouched."
  [model text]
  (str/replace text #"[a-zA-Z]+" (partial correct-word model)))
