// Popper is left out. Bootstrap's dropdown always creates a Popper instance, but for dropdowns in
// the menu bar (navbar) or with data-bs-display="static" it positions the menu with CSS and never
// uses it: this no-op instance is enough. Any other dropdown needs the real @popperjs/core.
export function createPopper() {
  return { destroy() {}, update() {}, setOptions() { return Promise.resolve(); }, forceUpdate() {}, state: {} };
}
