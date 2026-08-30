/**
 * Turns "/users/:id/posts/:postId" into a RegExp with a capture group for
 * each ":name" segment, plus the ordered list of names those groups
 * correspond to. A "*" segment captures the rest of the path under the
 * key "wildcard". Literal segments are regex-escaped so a path like
 * "/a.b" only matches a literal dot, not "any character".
 */
export function compilePath(path: string): { regex: RegExp; keys: string[] } {
  const keys: string[] = [];
  const segments = path.split("/").filter((s) => s.length > 0);

  const pattern = segments
    .map((segment) => {
      if (segment === "*") {
        keys.push("wildcard");
        return "(.*)";
      }
      if (segment.startsWith(":")) {
        keys.push(segment.slice(1));
        return "([^/]+)";
      }
      return segment.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
    })
    .join("/");

  return { regex: new RegExp(`^/${pattern}/?$`), keys };
}
