(ns twitter-bot.twitter
  "A tiny X (Twitter) API v2 client: POST /2/tweets, signed with OAuth 1.0a,
  over java.net.http. Failures come back as ex-info with a :type the bot
  can act on instead of a generic HTTP error."
  (:require [clojure.string :as str]
            [twitter-bot.json :as json]
            [twitter-bot.oauth :as oauth])
  (:import (java.net URI)
           (java.net.http HttpClient HttpRequest HttpRequest$BodyPublishers
                          HttpResponse HttpResponse$BodyHandlers)
           (java.time Duration)))

(def default-api-base "https://api.twitter.com")

(defn http-client []
  (-> (HttpClient/newBuilder)
      (.connectTimeout (Duration/ofSeconds 10))
      (.build)))

(defn- header [^HttpResponse resp name]
  (.orElse (.firstValue (.headers resp) name) nil))

(defn- parse-body [s]
  (try (json/read-str s) (catch Exception _ nil)))

(defn- error-detail [body]
  (or (:detail body) (:title body)
      (some-> body :errors first :message)
      "no detail"))

(defn classify-response
  "Turn status + headers + parsed body into either the created tweet
  {:id :text} or an ex-info. Split out from the HTTP call so every branch is
  testable without a server."
  [status headers body]
  (cond
    (<= 200 status 299)
    (let [{:keys [id text]} (:data body)]
      (if id
        {:id id :text text}
        (throw (ex-info "success status but no tweet id in response"
                        {:type :bad-response :status status :body body}))))

    (= status 429)
    (let [reset (some-> (get headers "x-rate-limit-reset") parse-long)]
      (throw (ex-info "rate limited" {:type :rate-limited :status status
                                      :reset-epoch reset :body body})))

    (= status 401)
    (throw (ex-info (str "unauthorized: " (error-detail body))
                    {:type :unauthorized :status status :body body}))

    ;; X answers a repeated tweet with 403, so the bot must tell "you said
    ;; that already" (try new text) from "this app may not post" (stop).
    (and (= status 403) (re-find #"(?i)duplicate" (str (error-detail body))))
    (throw (ex-info "duplicate content" {:type :duplicate :status status :body body}))

    (= status 403)
    (throw (ex-info (str "forbidden: " (error-detail body))
                    {:type :forbidden :status status :body body}))

    :else
    (throw (ex-info (str "HTTP " status ": " (error-detail body))
                    {:type :http-error :status status :body body}))))

(defn post-tweet!
  "Post `text` as the user the access token belongs to. Returns {:id :text}.
  Options: :api-base (point it at a fake server in tests), :client,
  :nonce and :timestamp (fixed values make requests reproducible)."
  [creds text & {:keys [api-base client nonce timestamp]
                 :or {api-base default-api-base}}]
  (let [url  (str (str/replace api-base #"/+$" "") "/2/tweets")
        auth (oauth/authorization-header creds :post url [] :nonce nonce :timestamp timestamp)
        req  (-> (HttpRequest/newBuilder (URI. url))
                 (.timeout (Duration/ofSeconds 20))
                 (.header "Authorization" auth)
                 (.header "Content-Type" "application/json")
                 (.header "User-Agent" "markov-bot/1.0")
                 (.POST (HttpRequest$BodyPublishers/ofString (json/write-str {:text text})))
                 (.build))
        ^HttpResponse resp (.send ^HttpClient (or client (http-client)) req
                                  (HttpResponse$BodyHandlers/ofString))]
    (classify-response (.statusCode resp)
                       {"x-rate-limit-reset" (header resp "x-rate-limit-reset")}
                       (parse-body (.body resp)))))
