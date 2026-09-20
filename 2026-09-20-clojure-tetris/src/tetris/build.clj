(ns tetris.build
  "Compiles tetris.ui (and the tetris.core it requires) to a single JS
   file the browser can load directly. Runs as `lein run` -- deliberately
   not the lein-cljsbuild plugin, since that plugin (and most ClojureScript
   tooling beyond the compiler itself) only ships on Clojars, which this
   sandbox can't reach; the ClojureScript compiler itself is on Maven
   Central, so driving it straight from cljs.build.api needs nothing else."
  (:require [cljs.build.api :as cljs]))

(defn -main [& _args]
  (println "Compiling tetris.ui -> resources/public/js/main.js ...")
  (cljs/build "src"
              {:main 'tetris.ui
               :output-to "resources/public/js/main.js"
               :output-dir "target/cljsbuild"
               :optimizations :simple
               :pretty-print false})
  (println "Done.")
  (System/exit 0))
