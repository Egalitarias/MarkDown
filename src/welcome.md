# Markdown Viewer

A native Windows viewer. Open a file with **Ctrl+O**, drag one onto the window, or pass a path on the command line.

## Features

- GitHub-style rendering: tables, task lists, alerts, strikethrough, autolinks
- Outline sidebar that follows your scroll position
- Find in document (**Ctrl+F**)
- Live reload when the file changes on disk
- Light / dark theme, zoom, wide layout
- Export to standalone HTML, print to PDF

> [!TIP]
> Edit the file in any editor and save. This window refreshes automatically.

## Shortcuts

| Shortcut | Action |
|----------|--------|
| Ctrl+O | Open file |
| Ctrl+U | Open URL |
| F5 / Ctrl+R | Reload |
| Ctrl+F | Find |
| Ctrl+B | Toggle outline |
| Ctrl+D | Toggle dark theme |
| Ctrl+L | Wide layout |
| Ctrl +/-/0 | Zoom |
| Ctrl+S | Export HTML |
| Ctrl+P | Print |

## Code

```cpp
#include <iostream>
int main() { std::cout << "Hello, world!\n"; }
```

- [x] Render markdown
- [ ] Write something great

<div align="center">

**Raw HTML blocks** work too, safely filtered.

<img src="https://placehold.co/160x40?text=HTML+img" alt="placeholder" width="160">

</div>

<details>
<summary>Footnotes and details</summary>

Footnotes are supported.[^1] They can be referenced more than once.[^1]

</details>

[^1]: Here is the footnote, rendered at the bottom with a back-link.
