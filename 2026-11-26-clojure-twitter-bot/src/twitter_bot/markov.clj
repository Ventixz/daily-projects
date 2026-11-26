(ns twitter-bot.markov
  "Word-level Markov chains: read a corpus, count which word follows each
  run of `order` words, then walk those counts at random to make new text."
  (:require [clojure.string :as str]))

;; ---------------------------------------------------------------------------
;; Tokenizing
;;
;; Words keep their own punctuation ("Alice," "said." "me?") so the chain
;; learns where commas and full stops go without any grammar. What it must
;; NOT keep is anything that has to be balanced: Carroll writes dialogue in
;; single quotes, so a token like 'I or said.' would make the bot open quotes
;; it never closes. Those characters are stripped from word edges.

(def ^:private edge-junk
  ;; Quotes, brackets and the _underscores_ Gutenberg uses for italics.
  #"^['\"()\[\]_*]+|['\"()\[\]_*]+$")

(defn- heading-line?
  "CHAPTER I. / ALICE'S ADVENTURES IN WONDERLAND / * * * -- lines with no
  lowercase letter are titles and dividers, not prose."
  [line]
  (not (re-find #"\p{Ll}" line)))

(defn- clean-token [t]
  (let [t (loop [t t]
            ;; Strip repeatedly: ('Oh!') needs two passes.
            (let [t' (str/replace t edge-junk "")]
              (if (= t t') t (recur t'))))]
    (cond
      (= t "—") t
      (re-find #"\p{L}" t) t
      :else nil)))

(defn tokenize
  "Split text into word tokens. A double hyphen (Carroll's dash) becomes its
  own em-dash token so 'mine--a' does not glue two words together."
  [text]
  (->> (str/split-lines text)
       (remove heading-line?)
       (mapcat #(str/split (str/replace % "--" " — ") #"\s+"))
       (keep clean-token)
       vec))

(defn sentence-end?
  "A word that finishes a sentence: ends in . ! or ?"
  [word]
  (boolean (re-find #"[.!?]$" word)))

(defn- capitalized? [word]
  (boolean (re-find #"^\p{Lu}" word)))

;; ---------------------------------------------------------------------------
;; Building the chain
;;
;; {:order  2
;;  :next   {["the" "Queen"] {:words ["said" "of" ...] :cum [3 5 ...] :total 9}}
;;  :starts [["Alice" "was"] ["The" "Queen"] ...]}
;;
;; Each prefix maps to its followers in a sorted vector with running totals,
;; so a weighted pick is one random number plus a binary search, and the same
;; seed always gives the same text (a hash map's iteration order would be an
;; implementation detail to depend on).

(defn- freqs->table [freqs]
  (let [words (vec (sort (keys freqs)))
        cum   (vec (reductions + (map freqs words)))]
    {:words words :cum cum :total (peek cum)}))

(defn build-chain
  "Count, for every `order`-word prefix in `tokens`, how often each word
  follows it. Start prefixes are those that open a sentence: the first one in
  the text, or any that comes right after a sentence-ending word, as long as
  it begins with a capital letter."
  ([tokens] (build-chain tokens 2))
  ([tokens order]
   {:pre [(pos-int? order)]}
   (let [tokens (vec tokens)
         n      (count tokens)
         counts (reduce (fn [m i]
                          (update-in m [(subvec tokens i (+ i order))
                                        (tokens (+ i order))]
                                     (fnil inc 0)))
                        {}
                        (range (max 0 (- n order))))
         starts (->> (range (max 0 (- n order)))
                     (filter (fn [i]
                               (and (or (zero? i) (sentence-end? (tokens (dec i))))
                                    (capitalized? (tokens i))
                                    (contains? counts (subvec tokens i (+ i order))))))
                     (mapv #(subvec tokens % (+ % order))))]
     {:order  order
      :next   (update-vals counts freqs->table)
      :starts starts})))

(defn corpus-windows
  "Every run of `k` consecutive tokens in the source, joined with spaces.
  Used to spot output that is a verbatim quote rather than a new sentence."
  [tokens k]
  (into #{} (map #(str/join " " %)) (partition k 1 tokens)))

;; ---------------------------------------------------------------------------
;; Walking the chain

(defn- pick
  "Weighted choice from a follower table using `rng` (java.util.Random)."
  [^java.util.Random rng {:keys [words cum total]}]
  (let [r (.nextInt rng (int total))]       ; 0 <= r < total
    ;; first index whose running total exceeds r
    (loop [lo 0 hi (dec (count cum))]
      (if (< lo hi)
        (let [mid (quot (+ lo hi) 2)]
          (if (> (cum mid) r) (recur lo mid) (recur (inc mid) hi)))
        (words lo)))))

(defn generate-sentence
  "Walk from a random start prefix until a sentence-ending word, giving up
  (nil) after `max-words`. Returns a vector of tokens."
  [{order :order followers :next starts :starts} ^java.util.Random rng
   & {:keys [max-words] :or {max-words 60}}]
  (when (seq starts)
    (loop [out (starts (.nextInt rng (count starts)))]
      (cond
        (sentence-end? (peek out)) out
        (>= (count out) max-words) nil
        :else (if-let [table (followers (subvec out (- (count out) order)))]
                (recur (conj out (pick rng table)))
                nil)))))                    ; dead end: prefix only at text end

;; ---------------------------------------------------------------------------
;; Tweet length, the way X counts it
;;
;; X does not count characters. It counts weighted code points after NFC
;; normalization: code points in a few "Latin-ish" ranges weigh 1, everything
;; else (CJK, emoji, ...) weighs 2, and the limit is 280. These ranges are the
;; defaults from the open-source twitter-text config v3. URLs (always 23) are
;; not handled because this corpus has none.

(def ^:private light-ranges
  [[0 4351] [8192 8205] [8208 8223] [8242 8247]])

(defn weighted-length [^String s]
  (let [s (java.text.Normalizer/normalize s java.text.Normalizer$Form/NFC)]
    (->> (.codePoints s)
         .toArray
         (map (fn [cp] (if (some (fn [[lo hi]] (<= lo cp hi)) light-ranges) 1 2)))
         (reduce + 0))))

(def tweet-limit 280)

(defn compose
  "Build one tweet: whole generated sentences, joined until adding another
  would pass `limit`. Sentences that are verbatim copies of the corpus (every
  `k`-word window appears in the source) are thrown away; a bot that just
  quotes the book is not generating anything. Returns nil if `attempts`
  sentences in a row all fail."
  [chain windows ^java.util.Random rng
   & {:keys [limit min-words max-sentences attempts]
      :or {limit tweet-limit min-words 6 max-sentences 3 attempts 200}}]
  (let [k      (+ (:order chain) 3)
        copied? (fn [words]
                  (every? #(contains? windows (str/join " " %))
                          (partition k 1 words)))
        fresh  (fn []
                 (loop [tries attempts]
                   (when (pos? tries)
                     (let [s (generate-sentence chain rng)]
                       (if (and s
                                (>= (count s) min-words)
                                (> (count s) k)       ; long enough to judge
                                (not (copied? s))
                                (<= (weighted-length (str/join " " s)) limit))
                         (str/join " " s)
                         (recur (dec tries)))))))]
    (when-let [first-sentence (fresh)]
      (loop [tweet first-sentence n 1]
        (if (>= n max-sentences)
          tweet
          (let [s (fresh)
                longer (when s (str tweet " " s))]
            (if (and longer
                     (<= (weighted-length longer) limit)
                     ;; stop early sometimes so tweets vary in length
                     (< (.nextDouble rng) 0.5))
              (recur longer (inc n))
              tweet)))))))
