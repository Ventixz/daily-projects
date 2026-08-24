import argparse
import os
import sys

from .index import InvertedIndex
from .query import BooleanQueryError, boolean_search
from .ranking import rank

DEFAULT_CORPUS = os.path.join(os.path.dirname(os.path.dirname(__file__)), "corpus")


def _print_snippet(index, doc_id, width=80):
    path = os.path.join(DEFAULT_CORPUS, doc_id)
    try:
        with open(path, encoding="utf-8") as f:
            first_line = f.readline().strip()
    except OSError:
        first_line = ""
    if len(first_line) > width:
        first_line = first_line[: width - 3] + "..."
    print(f"    {first_line}")


def main(argv=None):
    parser = argparse.ArgumentParser(prog="search-engine")
    parser.add_argument(
        "--corpus", default=DEFAULT_CORPUS, help="directory of .txt documents"
    )
    sub = parser.add_subparsers(dest="command", required=True)

    bool_p = sub.add_parser("bool", help="boolean AND/OR/NOT search")
    bool_p.add_argument("query", nargs="+")

    rank_p = sub.add_parser("search", help="ranked tf-idf / cosine similarity search")
    rank_p.add_argument("query", nargs="+")
    rank_p.add_argument("-k", "--top-k", type=int, default=5)

    args = parser.parse_args(argv)
    index = InvertedIndex.from_directory(args.corpus)
    query_str = " ".join(args.query)

    if args.command == "bool":
        try:
            results = sorted(boolean_search(index, query_str))
        except BooleanQueryError as e:
            print(f"error: {e}", file=sys.stderr)
            return 1
        if not results:
            print("no matches")
        for doc_id in results:
            print(doc_id)
            _print_snippet(index, doc_id)
        return 0

    if args.command == "search":
        results = rank(index, query_str, top_k=args.top_k)
        if not results:
            print("no matches")
        for doc_id, score in results:
            print(f"{doc_id}  (score={score:.4f})")
            _print_snippet(index, doc_id)
        return 0

    return 1


if __name__ == "__main__":
    sys.exit(main())
