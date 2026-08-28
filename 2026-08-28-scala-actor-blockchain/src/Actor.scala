package chain

import java.util.concurrent.LinkedBlockingQueue

/** Sentinel message that stops an actor's mailbox loop. */
private case object PoisonPill

/**
 * A minimal actor: its own thread, an unbounded mailbox, and a single-
 * threaded `receive` so state never needs a lock. `scala.actors` was
 * removed from the standard library after 2.10 and Akka lives on Maven
 * Central, which this box can't resolve without `sbt` (see LEARNING.md) --
 * a `LinkedBlockingQueue` plus a dedicated `Thread` is the whole
 * mechanism either of those wraps.
 */
abstract class Actor {
  private val mailbox = new LinkedBlockingQueue[Any]()
  @volatile private var running = false
  // Scala 2.11 has no SAM conversion (that's a 2.12+ feature), so a
  // `() => Unit` won't implicitly become a `Runnable` here.
  private val thread = new Thread(new Runnable { def run(): Unit = loop() })

  protected def receive: PartialFunction[Any, Unit]

  final def start(): this.type = {
    running = true
    thread.start()
    this
  }

  final def !(msg: Any): Unit = mailbox.put(msg)

  final def stop(): Unit = mailbox.put(PoisonPill)

  final def join(): Unit = thread.join()

  private def loop(): Unit = {
    while (running) {
      mailbox.take() match {
        case PoisonPill => running = false
        case msg        => if (receive.isDefinedAt(msg)) receive(msg)
      }
    }
  }
}
