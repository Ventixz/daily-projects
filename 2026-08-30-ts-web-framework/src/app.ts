import * as http from "http";
import { URL } from "url";
import { compilePath } from "./path";

export type Method = "GET" | "POST" | "PUT" | "PATCH" | "DELETE";

export interface Request extends http.IncomingMessage {
  params: Record<string, string>;
  query: Record<string, string>;
  path: string;
  body: unknown;
}

export interface Response extends http.ServerResponse {
  status(code: number): Response;
  json(data: unknown): void;
  send(data: string | Buffer): void;
}

export type NextFunction = (err?: unknown) => void;
export type Handler = (req: Request, res: Response, next: NextFunction) => void;
export type ErrorHandler = (
  err: unknown,
  req: Request,
  res: Response,
  next: NextFunction,
) => void;
export type Middleware = Handler | ErrorHandler;

/** An error with an HTTP status attached, used by the fallback error handler. */
export class HttpError extends Error {
  constructor(
    public status: number,
    message: string,
  ) {
    super(message);
  }
}

function statusOf(err: unknown): number {
  return err instanceof HttpError ? err.status : 500;
}

/**
 * A layer is one function on the stack plus the test that decides whether
 * it applies to a given request path. `method === null` means "any
 * method" (how app.use() differs from app.get()/app.post()/...).
 */
interface Layer {
  method: Method | null;
  test: (pathname: string) => Record<string, string> | null;
  handler: Middleware;
}

function isErrorHandler(fn: Middleware): fn is ErrorHandler {
  return fn.length === 4;
}

function prefixTest(prefix: string): (pathname: string) => Record<string, string> | null {
  const normalized = prefix === "/" ? "" : prefix.replace(/\/$/, "");
  return (pathname: string) => {
    if (normalized === "") return {};
    if (pathname === normalized || pathname.startsWith(normalized + "/")) return {};
    return null;
  };
}

function exactTest(path: string): (pathname: string) => Record<string, string> | null {
  const { regex, keys } = compilePath(path);
  return (pathname: string) => {
    const result = regex.exec(pathname);
    if (!result) return null;
    const params: Record<string, string> = {};
    keys.forEach((key, i) => (params[key] = decodeURIComponent(result[i + 1])));
    return params;
  };
}

async function readBody(req: http.IncomingMessage): Promise<Buffer> {
  const chunks: Buffer[] = [];
  for await (const chunk of req) {
    chunks.push(chunk as Buffer);
  }
  return Buffer.concat(chunks);
}

function augmentResponse(res: http.ServerResponse): Response {
  const r = res as Response;
  r.status = (code: number) => {
    r.statusCode = code;
    return r;
  };
  r.json = (data: unknown) => {
    const body = JSON.stringify(data);
    r.setHeader("Content-Type", "application/json; charset=utf-8");
    r.end(body);
  };
  r.send = (data: string | Buffer) => {
    if (!r.hasHeader("Content-Type")) {
      r.setHeader(
        "Content-Type",
        Buffer.isBuffer(data) ? "application/octet-stream" : "text/plain; charset=utf-8",
      );
    }
    r.end(data);
  };
  return r;
}

export class App {
  private stack: Layer[] = [];

  use(pathOrHandler: string | Middleware, ...rest: Middleware[]): this {
    if (typeof pathOrHandler === "string") {
      const test = prefixTest(pathOrHandler);
      for (const handler of rest) this.stack.push({ method: null, test, handler });
    } else {
      const test = prefixTest("/");
      for (const handler of [pathOrHandler, ...rest]) {
        this.stack.push({ method: null, test, handler });
      }
    }
    return this;
  }

  private on(method: Method, path: string, handlers: Handler[]): this {
    const test = exactTest(path);
    for (const handler of handlers) this.stack.push({ method, test, handler });
    return this;
  }

  get(path: string, ...handlers: Handler[]): this {
    return this.on("GET", path, handlers);
  }
  post(path: string, ...handlers: Handler[]): this {
    return this.on("POST", path, handlers);
  }
  put(path: string, ...handlers: Handler[]): this {
    return this.on("PUT", path, handlers);
  }
  patch(path: string, ...handlers: Handler[]): this {
    return this.on("PATCH", path, handlers);
  }
  delete(path: string, ...handlers: Handler[]): this {
    return this.on("DELETE", path, handlers);
  }

  /** Runs the stack for one request. Exposed directly so tests can drive it without a socket. */
  async handle(rawReq: http.IncomingMessage, rawRes: http.ServerResponse): Promise<void> {
    const req = rawReq as Request;
    const res = augmentResponse(rawRes);

    const url = new URL(req.url ?? "/", "http://internal");
    req.path = url.pathname;
    req.query = Object.fromEntries(url.searchParams);
    req.params = {};

    const raw = await readBody(req);
    const contentType = req.headers["content-type"] ?? "";
    if (raw.length === 0) {
      req.body = undefined;
    } else if (contentType.includes("application/json")) {
      try {
        req.body = JSON.parse(raw.toString("utf8"));
      } catch {
        return this.dispatch(req, res, 0, new HttpError(400, "invalid JSON body"));
      }
    } else {
      req.body = raw.toString("utf8");
    }

    this.dispatch(req, res, 0, undefined);
  }

  private dispatch(req: Request, res: Response, index: number, err: unknown): void {
    if (index >= this.stack.length) {
      if (err !== undefined) {
        if (!res.headersSent) {
          res
            .status(statusOf(err))
            .json({ error: err instanceof Error ? err.message : String(err) });
        }
      } else if (!res.headersSent) {
        res.status(404).json({ error: `Cannot ${req.method} ${req.path}` });
      }
      return;
    }

    const layer = this.stack[index];
    const next: NextFunction = (nextErr) => this.dispatch(req, res, index + 1, nextErr);
    const isErrLayer = isErrorHandler(layer.handler);

    if (err !== undefined) {
      if (!isErrLayer) return next(err);
      const params = layer.test(req.path);
      if (params === null) return next(err);
      req.params = { ...req.params, ...params };
      try {
        (layer.handler as ErrorHandler)(err, req, res, next);
      } catch (thrown) {
        next(thrown);
      }
      return;
    }

    if (isErrLayer) return next(undefined);
    if (layer.method !== null && layer.method !== req.method) return next(undefined);
    const params = layer.test(req.path);
    if (params === null) return next(undefined);
    req.params = { ...req.params, ...params };
    try {
      (layer.handler as Handler)(req, res, next);
    } catch (thrown) {
      next(thrown);
    }
  }

  listen(port: number, cb?: () => void): http.Server {
    const server = http.createServer((req, res) => {
      this.handle(req, res).catch((err) => {
        if (!res.headersSent) {
          res.statusCode = 500;
          res.end(JSON.stringify({ error: String(err) }));
        }
      });
    });
    return server.listen(port, cb);
  }
}
