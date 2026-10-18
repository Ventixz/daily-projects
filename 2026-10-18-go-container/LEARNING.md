# Learning: Container From Scratch in Go

**Source:** "Building a container from scratch in Go" (Liz Rice) from the Go section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
Go stdlib only, Linux only, needs root.

Run:
```
./make-rootfs.sh /tmp/rootfs            # copy sh/ls/ps/... plus their shared libs
go build -o minicontainer .
sudo ./minicontainer run --root /tmp/rootfs --hostname demo --pids 20 -- /bin/sh
```
Test: `go test -v ./...` (the isolation test skips itself when not root or without namespaces).

## Layout

- `main.go` — dispatches `run` (parent) and `child` (re-exec'd inside the namespaces); propagates exit codes.
- `container.go` — `ParseArgs`, `runParent` (clone flags), `runChild` (hostname, chroot, /proc mount, exec), `limitPids` (cgroup v2).
- `make-rootfs.sh` — builds a minimal root filesystem using `ldd` to find the libraries each binary needs.
- `container_test.go` — arg-parsing tests plus an integration test checking hostname, PID and filesystem isolation and exit codes.

## What I learned

- **A container is just a process with namespaces.** `CLONE_NEWUTS | CLONE_NEWPID | CLONE_NEWNS` give it its own hostname, process tree and mount table.
- **Why the re-exec trick:** a new PID namespace applies to children, not the caller, so the program runs `/proc/self/exe child ...` with the clone flags and finishes setup there.
- **chroot alone isn't enough:** `ps` needs a fresh `/proc` mounted inside the new PID namespace, and mounts must be made `MS_PRIVATE` first or they leak to the host.
- **Binaries need their libraries.** A rootfs with only `/bin/sh` fails with a confusing "not found"; `ldd` shows what to copy.
- **cgroups limit resources** (here `pids.max`) while namespaces limit visibility.

## Known limits

- Mid-way test surprise: I expected `echo $$` to print `1`, but the Go child is PID 1 and the shell is a child (PID 7). Using `syscall.Exec` instead of `exec.Command` would fix that, at the cost of unmounting `/proc` on exit.
- `chroot` is escapable by a root process in the container; real runtimes use `pivot_root`. No user namespace, network namespace or seccomp, so this is a demo, not a sandbox.
- The pids limit is best-effort (warns if cgroup v2 isn't writable) and the cgroup directory is not cleaned up.
