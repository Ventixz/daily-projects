import { App, Request, Response, NextFunction, HttpError } from "./app";

const app = new App();

// Global middleware: logging.
app.use((req: Request, _res: Response, next: NextFunction) => {
  console.log(`${req.method} ${req.path}`);
  next();
});

// Scoped middleware: a fake auth check, only for /admin/*.
app.use("/admin", (req: Request, res: Response, next: NextFunction) => {
  if (req.headers["x-api-key"] !== "let-me-in") {
    res.status(401).json({ error: "missing or bad x-api-key" });
    return;
  }
  next();
});

app.get("/", (_req, res) => {
  res.send("hello from a 20-line web framework");
});

app.get("/users/:id", (req, res) => {
  res.json({ id: req.params.id, query: req.query });
});

app.post("/echo", (req, res) => {
  res.json({ youSent: req.body });
});

app.get("/admin/dashboard", (_req, res) => {
  res.json({ ok: true, area: "admin" });
});

app.get("/boom", () => {
  throw new Error("thrown from a handler, on purpose");
});

// Error-handling middleware: four-argument functions are picked out by
// arity (see isErrorHandler() in app.ts) and only run when a prior
// handler passed (or threw) an error.
app.use("/", (err: unknown, _req: Request, res: Response, _next: NextFunction) => {
  const status = err instanceof HttpError ? err.status : 500;
  res.status(status).json({ error: err instanceof Error ? err.message : String(err) });
});

if (require.main === module) {
  const port = Number(process.env.PORT ?? 3000);
  app.listen(port, () => console.log(`listening on :${port}`));
}

export { app };
