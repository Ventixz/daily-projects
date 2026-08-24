import math

from .tokenizer import tokenize


def idf(index, term):
    """Inverse document frequency, natural log, 0 for unseen terms.

    A term absent from the whole corpus has document_frequency 0, which
    would divide by zero -- treat it as carrying no information instead
    of raising, since a query is allowed to contain words nothing in the
    corpus uses (it should just fail to boost anything).
    """
    df = index.document_frequency(term)
    if df == 0:
        return 0.0
    return math.log(index.num_docs / df)


def _log_tf(tf):
    """Sublinear tf scaling: 1 + ln(tf), 0 when the term is absent.

    Raw counts would let a document that repeats one word 50 times
    dominate cosine similarity; log-damping (the standard "ltc" weighting
    scheme) means going from 1 occurrence to 2 matters much more than
    going from 20 to 21.
    """
    return 1.0 + math.log(tf) if tf > 0 else 0.0


def term_vector(index, term_counts):
    """{term: tf-idf weight} for an arbitrary term->count mapping.

    Used for both document vectors (from index.doc_terms) and query
    vectors (from a tokenized query string) so both sides of the cosine
    similarity are built the same way.
    """
    return {
        term: _log_tf(tf) * idf(index, term)
        for term, tf in term_counts.items()
        if idf(index, term) > 0
    }


def _norm(vector):
    return math.sqrt(sum(w * w for w in vector.values()))


def cosine_similarity(vec_a, vec_b):
    if not vec_a or not vec_b:
        return 0.0
    shared_terms = vec_a.keys() & vec_b.keys()
    dot = sum(vec_a[t] * vec_b[t] for t in shared_terms)
    denom = _norm(vec_a) * _norm(vec_b)
    return dot / denom if denom else 0.0


def rank(index, query_str, top_k=10):
    """Rank documents by cosine similarity of tf-idf vectors.

    "Part 3: Ranking with tf-idf":
    http://www.ardendertat.com/2011/07/17/how-to-implement-a-search-engine-part-3-ranking-tf-idf/

    Only considers documents that share at least one query term (found
    via the inverted index's postings, not a full scan of every doc) --
    everything else has cosine similarity 0 by construction and would
    never make the top-k anyway.
    """
    query_terms = tokenize(query_str)
    if not query_terms:
        return []

    query_counts = {}
    for term in query_terms:
        query_counts[term] = query_counts.get(term, 0) + 1
    query_vec = term_vector(index, query_counts)
    if not query_vec:
        return []

    candidate_docs = set()
    for term in query_vec:
        candidate_docs |= index.docs_containing(term)

    scored = []
    for doc_id in candidate_docs:
        doc_vec = term_vector(index, index.doc_terms[doc_id])
        score = cosine_similarity(query_vec, doc_vec)
        if score > 0:
            scored.append((doc_id, score))

    scored.sort(key=lambda pair: (-pair[1], pair[0]))
    return scored[:top_k]
