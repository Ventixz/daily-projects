import os
from collections import Counter, defaultdict

from .tokenizer import tokenize


class InvertedIndex:
    """Maps term -> {doc_id: term_frequency}, built from a document set.

    This is the "Part 1: Create Index" piece of the tutorial this project
    is built from: http://www.ardendertat.com/2011/05/30/how-to-implement-a-search-engine-part-1-create-index/
    """

    def __init__(self):
        self.postings = defaultdict(dict)   # term -> {doc_id: tf}
        self.doc_terms = {}                  # doc_id -> {term: tf}
        self.doc_lengths = {}                # doc_id -> token count
        self.doc_names = {}                  # doc_id -> source name (filename or key)
        self.doc_ids = []

    @classmethod
    def from_directory(cls, corpus_dir):
        """Build an index from every *.txt file in corpus_dir."""
        docs = {}
        for name in sorted(os.listdir(corpus_dir)):
            if not name.endswith(".txt"):
                continue
            path = os.path.join(corpus_dir, name)
            with open(path, encoding="utf-8") as f:
                docs[name] = f.read()
        return cls.from_documents(docs)

    @classmethod
    def from_documents(cls, docs):
        """Build an index from a {doc_id: text} mapping, for tests."""
        idx = cls()
        for doc_id, text in docs.items():
            idx.add_document(doc_id, text)
        return idx

    def add_document(self, doc_id, text):
        if doc_id in self.doc_names:
            raise ValueError(f"duplicate doc_id: {doc_id!r}")
        tokens = tokenize(text)
        counts = Counter(tokens)
        for term, tf in counts.items():
            self.postings[term][doc_id] = tf
        self.doc_terms[doc_id] = dict(counts)
        self.doc_lengths[doc_id] = len(tokens)
        self.doc_names[doc_id] = doc_id
        self.doc_ids.append(doc_id)

    @property
    def num_docs(self):
        return len(self.doc_ids)

    def document_frequency(self, term):
        """Number of documents containing `term` at least once."""
        return len(self.postings.get(term, {}))

    def term_frequency(self, term, doc_id):
        return self.postings.get(term, {}).get(doc_id, 0)

    def docs_containing(self, term):
        """Set of doc_ids whose postings list includes `term`."""
        return set(self.postings.get(term, {}).keys())
