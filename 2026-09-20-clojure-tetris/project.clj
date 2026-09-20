(defproject tetris "0.1.0"
  :description "Tetris in ClojureScript -- practical-tutorials/project-based-learning"
  :dependencies [[org.clojure/clojure "1.11.1"]
                 [org.clojure/clojurescript "1.11.132"]]
  :source-paths ["src"]
  :test-paths ["test"]
  :main tetris.build
  :profiles {:uberjar {:aot :all}})
