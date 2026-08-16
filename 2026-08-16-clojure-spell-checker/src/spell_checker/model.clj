(ns spell-checker.model
  "Turns raw corpus text into the frequency table the corrector ranks
   candidates with."
  (:require [clojure.java.io :as io]
            [clojure.string :as str]))

(defn words
  "Lowercase words in `text`, stripped of surrounding punctuation.
   Mirrors Norvig's `re.findall('[a-z]+', text.lower())` -- an apostrophe
   inside a word (\"don't\") splits it into two tokens, same as his regex,
   so the model never learns \"don't\" as a single unit."
  [text]
  (re-seq #"[a-z]+" (str/lower-case text)))

(defn build-frequencies
  "word -> occurrence count across `text`."
  [text]
  (frequencies (words text)))

(defn load-corpus-frequencies
  "Read `resource-path` off the classpath and build its frequency table."
  [resource-path]
  (build-frequencies (slurp (io/resource resource-path))))

(def default-model
  "The frequency table built from resources/corpus.txt (Pride and
   Prejudice + Moby Dick), loaded once at class-load time."
  (delay (load-corpus-frequencies "corpus.txt")))
