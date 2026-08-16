(ns spell-checker.model-test
  (:require [clojure.test :refer [deftest is testing]]
            [spell-checker.model :as model]))

(deftest words-test
  (testing "lowercases and strips punctuation"
    (is (= ["the" "whale" "s" "tail"] (model/words "The Whale's Tail!"))))
  (testing "apostrophes split a word into two tokens, same as Norvig's regex"
    (is (= ["don" "t"] (model/words "Don't"))))
  (testing "digits and punctuation-only input yield no words -- re-seq
            returns nil rather than an empty seq when nothing matches"
    (is (nil? (model/words "12345 -- ...")))))

(deftest build-frequencies-test
  (is (= {"a" 2 "b" 1} (model/build-frequencies "a b a"))))

(deftest default-model-test
  (let [freqs @model/default-model]
    (testing "the corpus loaded and produced a non-trivial vocabulary"
      (is (> (count freqs) 5000)))
    (testing "'the' is the most frequent word in two 19th-century novels"
      (is (= "the" (key (apply max-key val freqs)))))
    (testing "a word absent from both novels is absent from the model"
      (is (not (contains? freqs "bicycle"))))))
