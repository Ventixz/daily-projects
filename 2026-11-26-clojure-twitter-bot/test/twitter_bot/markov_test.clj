(ns twitter-bot.markov-test
  (:require [clojure.java.io :as io]
            [clojure.string :as str]
            [clojure.test :refer [deftest is testing]]
            [twitter-bot.markov :as m]))

(deftest tokenize-keeps-punctuation-drops-quotes
  (is (= ["Oh," "dear!" "said" "Alice," "I" "shall" "be" "late!"]
         (m/tokenize "'Oh, dear!' said Alice, 'I shall be late!'")))
  (testing "nested brackets and italics underscores come off word edges"
    (is (= ["Oh!" "very" "much" "so"] (m/tokenize "('Oh!') _very_ much so"))))
  (testing "double hyphens become an em-dash token"
    (is (= ["mine" "—" "a" "Cheshire" "Cat"] (m/tokenize "mine--a Cheshire Cat"))))
  (testing "headings (no lowercase letters) and dividers are skipped"
    (is (= ["Down" "the" "hole."]
           (m/tokenize "CHAPTER I. DOWN THE RABBIT-HOLE\n\n  *    *    *\nDown the hole."))))
  (testing "apostrophes inside words stay"
    (is (= ["don't" "can't"] (m/tokenize "don't can't")))))

(deftest build-chain-counts-exactly
  (let [chain (m/build-chain (m/tokenize "The cat sat. The cat ran. A dog sat.") 2)]
    (is (= 2 (:order chain)))
    (is (= {:words ["ran." "sat."] :cum [1 2] :total 2}
           (get-in chain [:next ["The" "cat"]])))
    (is (= {:words ["The"] :cum [1] :total 1} (get-in chain [:next ["cat" "sat."]])))
    (testing "starts: first prefix, and capitalised prefixes after a full stop"
      (is (= [["The" "cat"] ["The" "cat"] ["A" "dog"]] (:starts chain))))
    (testing "the last prefix has no follower"
      (is (nil? (get-in chain [:next ["dog" "sat."]]))))))

(deftest weighted-pick-follows-counts
  ;; "a" -> b 3 times, c once: over many draws b should be ~75%.
  (let [chain (m/build-chain ["A" "b" "A" "b" "A" "b" "A" "c"] 1)
        table (get-in chain [:next ["A"]])
        rng   (java.util.Random. 1)
        draws (repeatedly 4000 #(#'m/pick rng table))
        share (/ (count (filter #{"b"} draws)) 4000.0)]
    (is (= {:words ["b" "c"] :cum [3 4] :total 4} table))
    (is (< 0.72 share 0.78) (str "share of b was " share))))

(deftest weighted-length-matches-x-rules
  (is (= 5 (m/weighted-length "hello")))
  (is (= 1 (m/weighted-length "—")) "em dash U+2014 is in a weight-1 range")
  (is (= 4 (m/weighted-length "日本")) "CJK counts double")
  (is (= 2 (m/weighted-length "😀")) "one emoji is one code point, weight 2")
  (testing "NFC first: e + combining acute is one code point"
    (is (= 1 (m/weighted-length "e\u0301")))))

(def corpus (delay (slurp (io/resource "corpus.txt"))))
(def tokens (delay (m/tokenize @corpus)))
(def chain  (delay (m/build-chain @tokens 2)))
(def windows (delay (m/corpus-windows @tokens 5)))

(deftest generated-sentences-only-use-real-transitions
  (let [rng (java.util.Random. 7)]
    (dotimes [_ 100]
      (when-let [s (m/generate-sentence @chain rng)]
        (is (some #{(subvec s 0 2)} (:starts @chain)))
        (is (m/sentence-end? (peek s)))
        (doseq [i (range 2 (count s))]
          (is (some #{(s i)} (get-in @chain [:next (subvec s (- i 2) i) :words]))
              (str "no transition " (subvec s (- i 2) i) " -> " (s i))))))))

(deftest tweets-fit-and-are-not-quotes
  (let [rng (java.util.Random. 2024)
        k   5
        tweets (repeatedly 200 #(m/compose @chain @windows rng))]
    (is (every? some? tweets))
    (doseq [t tweets]
      (is (<= (m/weighted-length t) 280) t)
      (is (re-find #"^\p{Lu}" t) t)
      (is (m/sentence-end? t) t)
      (testing "no sentence in it is copied whole from the book"
        (is (not (str/includes? (str/join " " @tokens) t)) t)))
    (testing "variety: not the same tweet over and over"
      (is (> (count (set tweets)) 190)))))

(deftest same-seed-same-text
  (let [gen #(let [rng (java.util.Random. 99)]
               (vec (repeatedly 5 (fn [] (m/compose @chain @windows rng)))))]
    (is (= (gen) (gen)))))

(deftest verbatim-sentences-are-rejected
  ;; Order 3 on a text where every prefix has one follower: the only thing
  ;; the chain can produce is the original sentence, so compose must give up.
  (let [toks (m/tokenize "The quick brown fox jumps over the lazy dog today.")
        c    (m/build-chain toks 3)]
    (is (= "The quick brown fox jumps over the lazy dog today."
           (str/join " " (m/generate-sentence c (java.util.Random. 1)))))
    (is (nil? (m/compose c (m/corpus-windows toks 6) (java.util.Random. 1) :attempts 20)))))

(deftest limit-is-respected-for-small-limits
  (let [rng (java.util.Random. 5)]
    (dotimes [_ 30]
      (when-let [t (m/compose @chain @windows rng :limit 80)]
        (is (<= (m/weighted-length t) 80) t)))))
