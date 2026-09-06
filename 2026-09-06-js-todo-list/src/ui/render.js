// Direct DOM rendering: on every state change, wipe the list and rebuild it
// from scratch. No diffing, on purpose -- this repo already has a project
// dedicated to *why* you'd build a diff/patch layer
// (../../2026-08-29-js-virtual-dom); this one is small enough that
// "render everything, every time" is the right amount of engineering, and
// re-implementing a mini vdom here would just bury that comparison instead
// of making it. What direct rendering costs you is exactly what that other
// project buys back: adding a diff layer here would let mid-edit browser
// state (the text you're actively editing right now, or focus for keyboard
// nav) survive a re-render caused by, say, a re-order in a different tab
// syncing in. renderList doesn't attempt that -- it uses roving tabindex and
// element ids so keyboard focus survives, but an in-flight edit does not.

function renderList(container, todos, handlers) {
  const focusedId = document.activeElement?.closest?.("[data-id]")?.dataset.id;
  const focusWasInEditor = document.activeElement?.classList?.contains("todo-edit");

  container.innerHTML = "";
  const list = document.createElement("ul");
  list.className = "todo-list";
  list.setAttribute("role", "list");

  todos.forEach((todo, index) => {
    list.appendChild(renderItem(todo, index, todos.length, handlers));
  });

  container.appendChild(list);

  if (focusedId) {
    const el = container.querySelector(`[data-id="${cssEscape(focusedId)}"]`);
    if (el) {
      const target = focusWasInEditor ? el.querySelector(".todo-edit") : el;
      target?.focus();
    }
  }
}

function cssEscape(id) {
  return window.CSS?.escape ? window.CSS.escape(id) : id.replace(/["\\]/g, "\\$&");
}

function renderItem(todo, index, count, handlers) {
  const li = document.createElement("li");
  li.className = "todo-item" + (todo.done ? " done" : "");
  li.dataset.id = todo.id;
  li.draggable = true;
  li.tabIndex = 0;
  li.setAttribute("role", "listitem");
  li.setAttribute("aria-posinset", String(index + 1));
  li.setAttribute("aria-setsize", String(count));

  const checkbox = document.createElement("input");
  checkbox.type = "checkbox";
  checkbox.checked = todo.done;
  checkbox.setAttribute("aria-label", `Mark "${todo.text}" ${todo.done ? "active" : "done"}`);
  checkbox.addEventListener("change", () => handlers.onToggle(todo.id));
  li.appendChild(checkbox);

  const text = document.createElement("span");
  text.className = "todo-text";
  text.textContent = todo.text;
  text.addEventListener("dblclick", () => beginEdit(li, todo, handlers));
  li.appendChild(text);

  const removeBtn = document.createElement("button");
  removeBtn.type = "button";
  removeBtn.className = "todo-remove";
  removeBtn.textContent = "\u00d7";
  removeBtn.setAttribute("aria-label", `Delete "${todo.text}"`);
  removeBtn.addEventListener("click", () => handlers.onRemove(todo.id));
  li.appendChild(removeBtn);

  li.addEventListener("keydown", (event) => onItemKeydown(event, li, todo, handlers));
  li.addEventListener("dragstart", (event) => {
    event.dataTransfer.setData("text/plain", todo.id);
    event.dataTransfer.effectAllowed = "move";
  });
  li.addEventListener("dragover", (event) => event.preventDefault());
  li.addEventListener("drop", (event) => {
    event.preventDefault();
    const draggedId = event.dataTransfer.getData("text/plain");
    if (!draggedId || draggedId === todo.id) return;
    const rect = li.getBoundingClientRect();
    const before = event.clientY < rect.top + rect.height / 2;
    const targetIndex = before ? index : index + 1;
    handlers.onReorder(draggedId, targetIndex);
  });

  return li;
}

function beginEdit(li, todo, handlers) {
  const text = li.querySelector(".todo-text");
  const input = document.createElement("input");
  input.type = "text";
  input.className = "todo-edit";
  input.value = todo.text;
  text.replaceWith(input);
  input.focus();
  input.setSelectionRange(input.value.length, input.value.length);

  const commit = () => handlers.onEditCommit(todo.id, input.value);
  input.addEventListener("blur", commit);
  input.addEventListener("keydown", (event) => {
    if (event.key === "Enter") input.blur();
    if (event.key === "Escape") {
      input.removeEventListener("blur", commit);
      handlers.onEditCommit(todo.id, todo.text); // no-op commit, restores view
    }
  });
}

// Roving tabindex: ArrowUp/ArrowDown move focus between items without
// changing any application state, so keyboard users can walk the list the
// same way a mouse user's eye does.
function onItemKeydown(event, li, todo, handlers) {
  if (event.key === "ArrowDown" || event.key === "ArrowUp") {
    event.preventDefault();
    const items = Array.from(li.parentElement.children);
    const i = items.indexOf(li);
    const next = event.key === "ArrowDown" ? items[i + 1] : items[i - 1];
    next?.focus();
  } else if (event.key === "Delete" || event.key === "Backspace") {
    if (document.activeElement === li) {
      event.preventDefault();
      handlers.onRemove(todo.id);
    }
  } else if (event.key === " " && document.activeElement === li) {
    event.preventDefault();
    handlers.onToggle(todo.id);
  }
}

export { renderList };
