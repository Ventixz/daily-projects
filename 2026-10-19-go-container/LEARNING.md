# Learning: A Container from Scratch, in Go

**Source:** "Building a container from scratch in Go" (Liz Rice) from the Go section of
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).
Reimplemented with the Go standard library only (Linux).

Try it (works rootless via a user namespace):

```
go build -o gocontainer .
./gocontainer mkrootfs rootfs sh hostname ps ls cat
./gocontainer run --rootfs rootfs --hostname demo -- /bin/sh -c 'hostname; echo pid=$$; ps'
```

Test: `go test ./...` (the end-to-end test skips itself if namespaces are unavailable).

## Layout

- `container.go`: platform-independent pieces: `ParseArgs`, cgroup v2 writes (`CgroupWrites`, `ApplyCgroup`),
  `MakeRootfs` (copies binaries and the shared libraries `ldd` reports), `LibsFromLdd`.
- `main_linux.go`: `run` (re-exec into new namespaces) and `child` (hostname, chroot, mount `/proc`, exec).
- `container_test.go`: unit tests for the above plus an end-to-end isolation test.

## What I learned

- **A container is just a process with namespaces.** `CLONE_NEWUTS`, `CLONE_NEWPID` and `CLONE_NEWNS` give it
  its own hostname, PID numbering and mount table. There is no "container" object in the kernel.
- **PID namespaces apply to children, hence the re-exec.** The parent starts `/proc/self/exe child ...` with the
  clone flags; the child is PID 1 and can then set up the environment before running the user's command.
- **Exec, don't fork, for the final step.** My first version ran the command as a child of the setup process, so
  `$$` printed 7, not 1 (threads also consume PIDs in the namespace). `syscall.Exec` replaces the setup process
  so the user's command really is PID 1.
- **Make mounts private before mounting anything.** Otherwise the `/proc` mount can propagate to the host.
- **`ps` needs its own `/proc`.** After chroot, mounting a fresh procfs inside the PID namespace is what makes
  `ps` show only container processes.
- **Rootless works with a user namespace.** Mapping the caller's uid to 0 inside gives "root" without real
  privileges, which is also what made this testable in CI-like sandboxes.
- **cgroups v2 limits are just file writes** (`memory.max`, `pids.max`, then the pid into `cgroup.procs`).
  Writing limits before moving the process in avoids a window where it is unlimited. Keeping this as a pure
  function of `Config` made it unit-testable against a temp dir.
- **A rootfs needs its dynamic libraries.** A copied `sh` is useless without `libc` and the loader, so
  `mkrootfs` parses `ldd` output.

## Known limits

- chroot, not `pivot_root`, so a process with enough privilege can escape; no seccomp or capability dropping.
- No network namespace, no `/dev`, no overlay filesystem or image format.
- Cgroup limits are best-effort (warning only) and assume a unified cgroup v2 hierarchy mounted at
  `/sys/fs/cgroup`; they are not applied in rootless mode here.
