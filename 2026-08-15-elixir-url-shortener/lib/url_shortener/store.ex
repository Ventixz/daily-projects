defmodule UrlShortener.Store do
  @moduledoc """
  Mnesia-backed storage for the shortener.

  Two tables, not one:

    * `:urls`     `{:urls, code, target}`     -- the code -> target mapping
    * `:url_hits` `{:url_hits, code, hits}`    -- a click counter per code

  Splitting them is what lets `resolve/1` bump the click counter with
  `:mnesia.dirty_update_counter/3`, which only accepts records shaped like
  `{Table, Key, Integer}`. A single `{:urls, code, target, hits}` table
  can't use that op at all -- `target` sitting between the key and the
  counter breaks the shape the op expects.
  """

  @code_alphabet ~c"0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
  @code_length 6
  @max_generate_attempts 20

  @doc """
  Sets up the on-disk schema and tables, then starts Mnesia.

  Declaring `:mnesia` in `extra_applications` (`mix.exs`) is what makes
  `mix xref` happy about every `:mnesia.*` call in this module, but it has
  a side effect: OTP starts every application on that list *before*
  calling `UrlShortener.Application.start/2`, which means Mnesia is
  already running -- as a fresh, schema-less, RAM-only node -- by the time
  this function gets to run. Calling `:mnesia.create_schema/1` against an
  already-running Mnesia is a no-op (it just reports the schema already
  exists), so the very first version of this function skipped `stop/0`
  and went straight to `create_schema` + `create_table` with
  `disc_copies: [node()]` -- which failed every time with
  `{:aborted, {:bad_type, :urls, :disc_copies, :nonode@nohost}}`, because
  the schema OTP had already brought up for this node was RAM-only and no
  amount of retrying `create_table` changes a schema that already exists.
  Stopping the auto-started instance first, writing the on-disk schema
  while nothing is running, and then starting Mnesia back up against that
  schema is what actually gets a disc-backed node.
  """
  def init_schema do
    :mnesia.stop()

    case :mnesia.create_schema([node()]) do
      :ok -> :ok
      {:error, {_node, {:already_exists, _node2}}} -> :ok
    end

    :ok = :mnesia.start()

    create_table(:urls, [:code, :target])
    create_table(:url_hits, [:code, :hits])

    :mnesia.wait_for_tables([:urls, :url_hits], 5_000)
  end

  defp create_table(name, attrs) do
    case :mnesia.create_table(name, attributes: attrs, disc_copies: [node()]) do
      {:atomic, :ok} -> :ok
      {:aborted, {:already_exists, ^name}} -> :ok
    end
  end

  @doc """
  Reserves a fresh random code for `target` and returns `{:ok, code}`.

  The insert-if-absent check and the write happen inside one
  `:mnesia.transaction/1`, using `:mnesia.wread/1` (a *write-locking* read)
  rather than plain `read/1`. A plain read takes only a read lock, so two
  transactions can both see "code is free" and both proceed to write it --
  the second write wins silently and the first caller's code now points at
  someone else's URL. `wread` takes the write lock up front, so the second
  transaction blocks until the first commits, then sees the row and retries
  with a fresh code instead of racing it.
  """
  def create_short(target) when is_binary(target) do
    generate_unique_code(@max_generate_attempts, target)
  end

  defp generate_unique_code(0, _target) do
    {:error, :exhausted}
  end

  defp generate_unique_code(attempts_left, target) do
    code = random_code()

    result =
      :mnesia.transaction(fn ->
        case :mnesia.wread({:urls, code}) do
          [] ->
            :mnesia.write({:urls, code, target})
            :mnesia.write({:url_hits, code, 0})
            :ok

          [_taken] ->
            :taken
        end
      end)

    case result do
      {:atomic, :ok} -> {:ok, code}
      {:atomic, :taken} -> generate_unique_code(attempts_left - 1, target)
    end
  end

  defp random_code do
    for _ <- 1..@code_length, into: "", do: <<Enum.random(@code_alphabet)>>
  end

  @doc """
  Looks up `code` and atomically bumps its click counter.

  The lookup itself is a `:mnesia.dirty_read/1` (no transaction, no lock)
  because a stale-by-a-few-microseconds answer to "does this code exist"
  is harmless -- worst case a redirect is served a moment before a
  concurrent delete would have 404'd it instead. The counter bump is not
  allowed that same looseness: it uses `dirty_update_counter/3`, which
  Mnesia implements as a single atomic increment on the table, not a
  read-then-write. An earlier version of this function did
  `[{:url_hits, ^code, n}] = :mnesia.dirty_read(:url_hits, code)` followed
  by `:mnesia.dirty_write({:url_hits, code, n + 1})` -- two separate dirty
  operations with no lock between them. `test/url_shortener/store_test.exs`
  fires 100 concurrent `resolve/1` calls at the same code and asserts the
  final count is exactly 100; against the read-then-write version that
  assertion fails (usually landing in the 60-90 range, since a "dirty"
  read+write pair openly races), because two processes can both read `n`
  before either writes `n + 1`, and one increment is lost. Swapping in
  `dirty_update_counter/3` makes the increment itself indivisible, so the
  race has nothing to land in.
  """
  def resolve(code) when is_binary(code) do
    case :mnesia.dirty_read(:urls, code) do
      [{:urls, ^code, target}] ->
        :mnesia.dirty_update_counter(:url_hits, code, 1)
        {:ok, target}

      [] ->
        :not_found
    end
  end

  def hits(code) when is_binary(code) do
    case :mnesia.dirty_read(:url_hits, code) do
      [{:url_hits, ^code, hits}] -> {:ok, hits}
      [] -> :not_found
    end
  end

  def delete(code) when is_binary(code) do
    :mnesia.transaction(fn ->
      :mnesia.delete({:urls, code})
      :mnesia.delete({:url_hits, code})
    end)

    :ok
  end

  def list do
    :mnesia.transaction(fn ->
      :mnesia.foldl(fn {:urls, code, target}, acc -> [{code, target} | acc] end, [], :urls)
    end)
    |> case do
      {:atomic, rows} -> Enum.sort(rows)
    end
  end
end
