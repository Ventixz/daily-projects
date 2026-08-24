from .tokenizer import tokenize

OPERATORS = {"AND", "OR", "NOT"}


class BooleanQueryError(ValueError):
    pass


def boolean_search(index, query_str):
    """Evaluate a boolean query against an InvertedIndex.

    Grammar (deliberately simple -- no parentheses, no precedence, no
    stopword filtering on operands): a bare query is an implicit AND of
    its stopword-filtered terms, e.g. "python programming" behaves the
    same as "python AND programming". Explicit AND/OR/NOT are applied
    left to right against the running result set, e.g.
    "ocean AND deep NOT submarine" first intersects ocean & deep, then
    subtracts anything containing submarine.

    This is "Part 2: Query the Index":
    http://www.ardendertat.com/2011/05/31/how-to-implement-a-search-engine-part-2-query-index/
    """
    raw_tokens = query_str.split()
    if not raw_tokens:
        return set()

    parsed = []  # list of (operator_or_None, term)
    pending_op = None
    for raw in raw_tokens:
        upper = raw.upper()
        if upper in OPERATORS:
            if pending_op is not None:
                raise BooleanQueryError(f"two operators in a row near {raw!r}")
            pending_op = upper
            continue
        term = tokenize(raw, drop_stopwords=False)
        if not term:
            continue
        parsed.append((pending_op, term[0]))
        pending_op = None

    if pending_op is not None:
        raise BooleanQueryError(f"query cannot end with operator {pending_op}")
    if not parsed:
        return set()

    first_op, first_term = parsed[0]
    if first_op == "NOT":
        raise BooleanQueryError("query cannot start with NOT")
    result = index.docs_containing(first_term)

    for op, term in parsed[1:]:
        docs = index.docs_containing(term)
        op = op or "AND"
        if op == "AND":
            result &= docs
        elif op == "OR":
            result |= docs
        elif op == "NOT":
            result -= docs
        else:
            raise BooleanQueryError(f"unknown operator {op}")

    return result
