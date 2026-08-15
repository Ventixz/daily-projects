defmodule UrlShortener.StoreTest do
  use ExUnit.Case, async: false

  alias UrlShortener.Store

  test "create_short then resolve round-trips the target" do
    {:ok, code} = Store.create_short("https://example.com/a")
    assert {:ok, "https://example.com/a"} = Store.resolve(code)
  end

  test "generated codes are six characters" do
    {:ok, code} = Store.create_short("https://example.com/b")
    assert String.length(code) == 6
  end

  test "resolving an unknown code returns :not_found" do
    assert :not_found = Store.resolve("zzzzzz-does-not-exist")
  end

  test "each create_short call gets a distinct code" do
    codes = for _ <- 1..50, do: elem(Store.create_short("https://example.com/dup"), 1)
    assert length(Enum.uniq(codes)) == 50
  end

  test "hits starts at zero and is unaffected by create_short alone" do
    {:ok, code} = Store.create_short("https://example.com/c")
    assert {:ok, 0} = Store.hits(code)
  end

  test "resolve bumps the hit counter by exactly one per call" do
    {:ok, code} = Store.create_short("https://example.com/d")
    Store.resolve(code)
    Store.resolve(code)
    Store.resolve(code)
    assert {:ok, 3} = Store.hits(code)
  end

  test "delete removes both the mapping and the hit counter" do
    {:ok, code} = Store.create_short("https://example.com/e")
    Store.resolve(code)
    :ok = Store.delete(code)

    assert :not_found = Store.resolve(code)
    assert :not_found = Store.hits(code)
  end

  test "list returns every stored code/target pair" do
    {:ok, code} = Store.create_short("https://example.com/f")
    assert {code, "https://example.com/f"} in Store.list()
  end

  # This is the test that pins the lesson in the moduledoc for `resolve/1`:
  # 100 processes hit the same code's redirect at once, and every single
  # click has to land. It is the test that fails (usually 60-90, not 100)
  # against a `dirty_read` + `dirty_write` version of the counter bump,
  # and passes reliably once that's `dirty_update_counter/3` instead --
  # see the comment on `Store.resolve/1` for why.
  test "100 concurrent resolves against the same code all count" do
    {:ok, code} = Store.create_short("https://example.com/hot")

    1..100
    |> Task.async_stream(fn _ -> Store.resolve(code) end, max_concurrency: 100)
    |> Stream.run()

    assert {:ok, 100} = Store.hits(code)
  end
end
