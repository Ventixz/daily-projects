(ns spell-checker.correct-test
  (:require [clojure.test :refer [deftest is testing]]
            [spell-checker.correct :as correct]
            [spell-checker.model :as model]))

(def ^:private freqs @model/default-model)

(deftest edits1-test
  (testing "produces variants of every edit type"
    (let [e1 (correct/edits1 "cat")]
      (is (contains? e1 "at"))     ; delete 'c'
      (is (contains? e1 "act"))    ; transpose 'c' and 'a'
      (is (contains? e1 "bat"))    ; replace 'c' with 'b'
      (is (contains? e1 "cats"))   ; insert 's' at the end
      (is (contains? e1 "scat")))) ; insert 's' at the start
  (testing "replacing a letter with itself is a no-op, so a word is always
            among its own 1-edit neighbors"
    (is (contains? (correct/edits1 "cat") "cat")))
  (testing "count for a word with no repeated letters: n deletes + (n-1)
            transposes + 26n replaces + 26(n+1) inserts, minus the (n-1)
            duplicate same-letter replaces that all collapse onto the
            original word, minus the n insert/delete boundary duplicates
            that arise once per distinct letter"
    (is (= 494 (count (correct/edits1 "something"))))))

(deftest edits2-test
  (testing "two edits away is roughly two orders of magnitude larger than
            one edit away, unfiltered -- which is exactly why `known` has
            to run before this set is used for anything"
    (is (> (count (correct/edits2 "something")) 100000))))

(deftest known-test
  (is (= #{"the" "whale"} (correct/known freqs ["the" "whale" "zzqzzq"]))))

(deftest candidates-test
  (testing "an already-known word is its own sole candidate"
    (is (= #{"whale"} (correct/candidates freqs "whale"))))
  (testing "a word with no known neighbor within two edits falls back to itself"
    (is (= #{"zzzqzzzqzzzq"} (correct/candidates freqs "zzzqzzzqzzzq")))))

(deftest correction-test
  (testing "classic single-edit typos, correctable from the corpus vocabulary"
    (is (= "spelling" (correct/correction freqs "speling")))
    (is (= "the" (correct/correction freqs "teh")))
    (is (= "receive" (correct/correction freqs "recieve"))))
  (testing "classic two-edit typos"
    (is (= "inconvenient" (correct/correction freqs "inconvient")))
    (is (= "arranged" (correct/correction freqs "arrainged")))
    (is (= "poetry" (correct/correction freqs "peotry"))))
  (testing "an already-correct word is returned unchanged"
    (is (= "word" (correct/correction freqs "word")))
    (is (= "whale" (correct/correction freqs "whale"))))
  (testing "a modern word absent from either novel can't be reached, even
            though it's a real 2-edit typo, because `known` only accepts
            words the corpus actually contains"
    (is (= "korrectud" (correct/correction freqs "korrectud")))
    (is (= "bycycle" (correct/correction freqs "bycycle"))))
  (testing "when two known words are equally one edit away, the more
            frequent one wins even if it's the wrong one -- 'with' outranks
            'which' in this corpus by nearly 2 to 1, and raw frequency is
            all `correction` ranks by"
    (is (< (get freqs "which") (get freqs "with")))
    (is (= "with" (correct/correction freqs "wich")))))

(deftest correct-word-test
  (testing "case is preserved independently of the correction"
    (is (= "Spelling" (correct/correct-word freqs "Speling")))
    (is (= "SPELLING" (correct/correct-word freqs "SPELING")))
    (is (= "whale" (correct/correct-word freqs "whale")))))

(deftest correct-text-test
  (testing "punctuation, whitespace, and apostrophes pass through untouched;
            only the letter runs are corrected"
    (is (= "Spelling errors HAPPEN, and Ishmael's Whale-Boat had a problem."
           (correct/correct-text
            freqs
            "Speling errors HAPPEN, and Ishmael's Whale-Boat had a probblem.")))))
