(ns spell-checker.core
  "Command-line entry point.

   `lein run -- word1 word2 ...`  corrects each argument as a standalone
                                   word and prints `original -> correction`
   `lein run` with no arguments    reads a passage from stdin and prints
                                   it back with every misspelled word
                                   corrected in place, punctuation and
                                   capitalization untouched"
  (:require [spell-checker.correct :as correct]
            [spell-checker.model :as model])
  (:gen-class))

(defn -main [& args]
  (let [freqs @model/default-model]
    (if (seq args)
      (doseq [word args]
        (println (str word " -> " (correct/correct-word freqs word))))
      (print (correct/correct-text freqs (slurp *in*))))
    (flush)))
