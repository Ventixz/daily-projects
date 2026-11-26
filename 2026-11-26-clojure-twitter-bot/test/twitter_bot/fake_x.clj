(ns twitter-bot.fake-x
  "A local stand-in for the X API. It checks OAuth signatures the way the
  real service does -- with its own, separate implementation, so a bug in
  twitter-bot.oauth cannot hide by being checked against itself -- and
  rejects stale timestamps and reused nonces."
  (:require [clojure.string :as str]
            [twitter-bot.json :as json])
  (:import (com.sun.net.httpserver HttpExchange HttpHandler HttpServer)
           (java.net InetSocketAddress URLDecoder URLEncoder)
           (java.nio.charset StandardCharsets)
           (java.util Base64)
           (javax.crypto Mac)
           (javax.crypto.spec SecretKeySpec)))

(defn- enc [s]
  (-> (URLEncoder/encode (str s) StandardCharsets/UTF_8)
      (.replace "+" "%20") (.replace "*" "%2A") (.replace "%7E" "~")))

(defn parse-auth-header [h]
  (when (and h (str/starts-with? h "OAuth "))
    (into {} (for [part (str/split (subs h 6) #",\s*")
                   :let [[_ k v] (re-matches #"([^=]+)=\"(.*)\"" part)]
                   :when k]
               [(URLDecoder/decode k StandardCharsets/UTF_8)
                (URLDecoder/decode v StandardCharsets/UTF_8)]))))

(defn expected-signature [method url oauth consumer-secret token-secret]
  (let [params (->> (dissoc oauth "oauth_signature")
                    (map (fn [[k v]] (str (enc k) "=" (enc v))))
                    sort
                    (str/join "&"))
        base   (str method "&" (enc url) "&" (enc params))
        mac    (Mac/getInstance "HmacSHA1")]
    (.init mac (SecretKeySpec. (.getBytes (str (enc consumer-secret) "&" (enc token-secret))
                                          StandardCharsets/UTF_8) "HmacSHA1"))
    (.encodeToString (Base64/getEncoder) (.doFinal mac (.getBytes base StandardCharsets/UTF_8)))))

(defn- send! [^HttpExchange ex status headers body]
  (let [bytes (.getBytes (json/write-str body) StandardCharsets/UTF_8)]
    (doseq [[k v] headers] (.add (.getResponseHeaders ex) k (str v)))
    (.add (.getResponseHeaders ex) "Content-Type" "application/json")
    (.sendResponseHeaders ex status (alength bytes))
    (with-open [o (.getResponseBody ex)] (.write o bytes))))

(defn start!
  "Start the fake on a free port. `respond` is (fn [request] -> [status
  headers body]) for requests that pass authentication; by default every
  tweet is created with an increasing id. Returns a map with :base-url,
  :requests (atom), and :stop."
  [{:keys [consumer-key consumer-secret token token-secret]}
   & {:keys [respond clock] :or {clock #(quot (System/currentTimeMillis) 1000)}}]
  (let [server   (HttpServer/create (InetSocketAddress. "127.0.0.1" 0) 0)
        port     (.getPort (.getAddress server))
        base-url (str "http://127.0.0.1:" port)
        requests (atom [])
        nonces   (atom #{})
        ids      (atom 1000)
        respond  (or respond
                     (fn [{:keys [body]}]
                       [201 {} {:data {:id (str (swap! ids inc)) :text (:text body)}}]))]
    (.createContext
     server "/"
     (reify HttpHandler
       (handle [_ ex]
         (try
           (let [method (.getRequestMethod ex)
                 path   (.getPath (.getRequestURI ex))
                 raw    (String. (.readAllBytes (.getRequestBody ex)) StandardCharsets/UTF_8)
                 auth   (parse-auth-header (.getFirst (.getRequestHeaders ex) "Authorization"))
                 req    {:method method :path path :auth auth :raw-body raw
                         :content-type (.getFirst (.getRequestHeaders ex) "Content-Type")
                         :body (try (json/read-str raw) (catch Exception _ nil))}
                 _      (swap! requests conj req)
                 sig-ok (and auth
                             (= (get auth "oauth_consumer_key") consumer-key)
                             (= (get auth "oauth_token") token)
                             (= (get auth "oauth_signature")
                                (expected-signature method (str base-url path) auth
                                                    consumer-secret token-secret)))
                 fresh  (some-> (get auth "oauth_timestamp") parse-long
                                (#(<= (abs (- (clock) %)) 300)))
                 replay (contains? @nonces (get auth "oauth_nonce"))]
             (cond
               (not (and (= method "POST") (= path "/2/tweets")))
               (send! ex 404 {} {:title "Not Found"})
               (or (not sig-ok) (not fresh) replay)
               (send! ex 401 {} {:title "Unauthorized" :status 401
                                 :detail (cond (not sig-ok) "bad signature"
                                               (not fresh) "stale timestamp"
                                               :else "nonce reused")})
               :else
               (do (swap! nonces conj (get auth "oauth_nonce"))
                   (let [[status headers body] (respond req)]
                     (send! ex status headers body)))))
           (catch Throwable t
             (.printStackTrace t)
             (send! ex 500 {} {:title (str t)}))))))
    (.start server)
    {:base-url base-url
     :requests requests
     :stop #(.stop server 0)}))
