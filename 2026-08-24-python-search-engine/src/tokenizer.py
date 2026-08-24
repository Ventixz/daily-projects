import re

_TOKEN_RE = re.compile(r"[a-z0-9]+(?:'[a-z]+)?")

STOPWORDS = frozenset(
    """
    a an the and or but if then else for while to of in on at by with
    is are was were be been being this that these those it its as from
    not no do does did have has had i you he she we they them his her
    our your their
    """.split()
)


def tokenize(text, drop_stopwords=True):
    """Lowercase, strip punctuation, split into word tokens.

    Keeps internal apostrophes (don't, it's) but strips everything else
    that isn't a letter or digit. Returns a list, not a set -- callers
    that need term frequency counts rely on duplicates surviving.
    """
    tokens = _TOKEN_RE.findall(text.lower())
    if drop_stopwords:
        tokens = [t for t in tokens if t not in STOPWORDS]
    return tokens
