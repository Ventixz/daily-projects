(ns spell-checker.test-runner
  "There's no lein/clj network access in this environment to pull in a
   proper test-runner library, so this is a five-line one: require the
   test namespaces, run them, and exit non-zero on failure so `make test`
   fails CI the normal way."
  (:require [clojure.test :as test]
            [spell-checker.correct-test]
            [spell-checker.model-test])
  (:gen-class))

(defn -main [& _args]
  (let [{:keys [fail error]} (test/run-tests 'spell-checker.model-test
                                              'spell-checker.correct-test)]
    (System/exit (if (zero? (+ fail error)) 0 1))))
