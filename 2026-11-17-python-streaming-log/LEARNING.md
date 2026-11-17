# Learning: a Kafka-style distributed log in Python (standard library only)

**Source:** "Build a Distributed Streaming System with Python and Kafka" (codequs.com), from the Python /
Miscellaneous section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
The tutorial uses a real Apache Kafka cluster and a Python client library. codequs.com was blocked from the build
environment, so this follows the tutorial's idea, not its text. And instead of *using* Kafka, it builds the parts
that make Kafka work, **with no dependencies at all** (Python 3.10+ standard library only): a segmented,
crash-safe commit log, a TCP broker, key-based partitioning, consumer groups with rebalancing and fencing,
committed offsets, retention, and a follower that replicates the leader.

> Written by the daily-project routine (Claude). Commits are authored as `Claude`, so they are separate
> from the repo owner's own work in the history.

Run (four terminals, from this folder):

```
python3 -m streamlog broker --data ./data/leader --listen 127.0.0.1:9092
python3 -m streamlog broker --data ./data/follower --listen 127.0.0.1:9093 --follow 127.0.0.1:9092
python3 -m streamlog create orders --partitions 3
printf 'alice\tbought tea\nbob\tbought jam\nalice\tbought cups\n' | python3 -m streamlog produce orders --keyed
python3 -m streamlog consume orders --group billing     # start a second one to watch them split the partitions
python3 -m streamlog metadata --broker 127.0.0.1:9093   # the follower has the same offsets
```

Test: `python3 -m unittest discover -s tests -t .` runs 35 tests (about 13 s): 12 for the log, 12 for the
broker (9 over real sockets, 3 for partition assignment and the partitioner), 7 for consumer groups, and 4 for
replication.

## Layout

- `streamlog/log.py`: the storage engine. `Segment` is one `.log` file plus a sparse `.index`. `PartitionLog` is
  an ordered list of segments with one writer, many readers, segment rolling, retention, crash recovery, and
  `wait_for()` for long-polling.
- `streamlog/protocol.py`: framing. A u32 length followed by a JSON object. Keys and values are base64 in the JSON.
- `streamlog/broker.py`: `Broker` (topics, produce/fetch, request dispatch, threaded TCP server), `Coordinator`
  (consumer groups), `OffsetStore` (committed offsets), `range_assign`, and `partition_for`.
- `streamlog/client.py`: `Connection`, `Producer`, and a group-aware `Consumer` with `poll()` / `commit()`.
- `streamlog/replica.py`: `Replicator`, which a follower broker runs to copy the leader.
- `streamlog/__main__.py`: the CLI shown above.

APIs: `create_topic`, `metadata`, `produce`, `fetch` (with `max_wait_ms` long-poll), `join_group`, `heartbeat`,
`leave_group`, `commit`, `committed`.

## What I learned

- **A topic is a set of append-only files, and an offset is just a position in one of them.** Every record in a
  partition gets the next integer offset, and that never changes. Consumers aren't tracked by the broker while
  they read. A consumer is just "a number per partition", and reading is "give me records from offset N". That's
  why many independent groups can read the same data at different speeds, and why replaying is just resetting
  a number (`test_independent_groups_each_get_all_messages`).
- **Segments make deletion cheap.** You can't delete the front of a file, so the log is split into segment files
  named after their first offset (`00000000000000001234.log`). A new segment starts once the active one reaches
  `segment_bytes`. Retention then deletes whole old files, and the partition's start offset moves forward.
  Reading an offset that retention already removed is an `offset_out_of_range` error, and the consumer falls back
  to its reset policy (`earliest` or `latest`).
- **The index is sparse, and it's a cache.** Every `index_interval` bytes the segment writes one
  `(relative offset, file position)` pair. To read offset N, binary-search the index for the closest entry at or
  before N, seek there, and scan forward. Because it can be rebuilt from the log at any time, recovery just
  *rebuilds* it. A test fills the index file with `0xFF` and reads still work.
- **Crash safety comes from a length and a CRC on every record.** A crash in the middle of a write leaves a torn
  tail. On startup each segment is scanned, and the first record whose length runs past the end of the file,
  whose CRC doesn't match, or whose offset isn't the next expected one marks the end of valid data. The file is
  truncated there, so the next append continues cleanly from the right offset. Both the torn-write test and the
  corrupt-byte test fail if the CRC check or the truncation is removed.
- **Choosing a partition from the key is what makes ordering per key.** `crc32(key) % partitions`: every event for
  `user-42` goes to the same partition, so it is read back in the order it was written. Kafka only promises
  ordering *within* a partition, and this is how applications get the ordering they actually care about. Records
  without a key are spread round-robin. (Changing the partition count would move keys to different partitions,
  which is why the broker refuses to "re-create" a topic with a different count.)
- **Long-polling is a condition variable.** `fetch` with `max_wait_ms` blocks on the partition's
  `threading.Condition` until an append calls `notify_all()` or the time runs out. Consumers get messages almost
  as soon as they're written, without busy polling. The test produces 200 ms into a 5 s wait and checks the fetch
  returns in under 3 s.
- **A consumer group is coordinated through a generation number.** Each join, leave or expired session increases
  the group's generation and recomputes the assignment (Kafka's `range` strategy: contiguous chunks of each
  topic's partitions over the sorted members, with earlier members getting the extras). A member holding an old
  generation is told `rebalance_in_progress` on its next heartbeat, and rejoins to get its new partitions. A
  member that stops heartbeating for longer than the session timeout is removed, and its partitions go to the
  others (`test_crashed_member_is_expired...`).
- **Fencing: a commit from an old generation is refused.** Suppose a consumer pauses (GC, a slow request) long
  enough to be removed, and its partitions are handed to someone else. When it wakes up, it must not commit its
  old position over the new owner's progress. `commit` carries `(member_id, generation)`, and the broker rejects
  it if the generation is out of date. `test_zombie_commit_is_fenced` tries exactly this, and the committed offset
  stays at 8.
- **"At-least-once" is about *when* you commit.** Commit after processing. If a consumer crashes after
  processing but before committing, the next owner starts from the last commit and processes those messages
  again (`test_uncommitted_work_is_redelivered_after_handover`, which sees offsets 2–5 again). Committing before
  processing would give at-most-once instead. Exactly-once needs transactions, which this doesn't have.
- **A follower is a consumer that keeps offsets.** Replication reuses the normal `fetch` API. The follower reads
  each partition from its own end offset and appends with `append_replica`, which refuses any gap, so both
  brokers have exactly the same offsets, keys, values and timestamps. If the follower has fallen behind the
  leader's retention, it throws away its copy and restarts the partition at the leader's start offset.
- **Publish shared state only once it's complete.** The first test run found a real race. `_open_topic` added
  the topic name to `self.topics` *before* opening its partition logs, so another thread could see the topic and
  then get a `KeyError` looking up a log. The fix is to open everything first and publish the name last.

## Testing notes

To check that the tests actually test something, I broke the code on purpose, one change at a time: turned off
the CRC check, the torn-tail truncation, commit fencing, session expiry, key-based partitioning, the long-poll
wakeup, resuming from the committed offset, and retention. Every one of those changes made at least one test
fail.

Two group tests failed on the first run because they relied on timing: they called `poll()` expecting a
heartbeat, but heartbeats only go out every `heartbeat_interval`. `heartbeat()` is now public and the tests call
it directly, so they don't depend on timing. The suite passed 3 runs in a row afterwards.

## Known limits

- **Rebalancing is simplified.** Kafka's coordinator waits for every member to rejoin before handing out new
  partitions. Here the new assignment takes effect immediately, so for up to one heartbeat interval an old owner
  and a new owner can both read the same partition. Fencing keeps the *committed* offsets correct, but some
  messages can be processed twice (allowed under at-least-once, but more than Kafka would do).
- **Replication is asynchronous, with one fixed leader.** There's no `acks=all`, no in-sync replica set, no high
  watermark, and no automatic leader election. A record acknowledged by the leader can be lost if the leader's
  disk dies before the follower copies it. Failing over means restarting the follower as a leader by hand.
- Appends are flushed to the OS but not `fsync`ed by default (`PartitionLog(fsync=True)` turns that on), so a
  power cut can lose the last few writes. Recovery still gives a consistent prefix.
- JSON plus base64 framing is easy to read and debug, but it adds about 33% to every value. Kafka uses a binary
  protocol and batches records with compression.
- One request at a time per connection, with no authentication or TLS. Committed offsets are kept in a JSON-lines
  file that is compacted on startup, not in an internal topic.
