# Markdown Viewer

Native Win32 (C++) Markdown viewer. Markdown is parsed in C++ (`src/markdown.cpp`) and displayed with WebView2.
Open local files or URLs, live reload, outline, find, dark mode, export to HTML, print.

## Build

1. Install Visual Studio 2022 (C++ workload).
2. Download the WebView2 SDK into `third_party/webview2` (NuGet package `Microsoft.Web.WebView2`, e.g. 1.0.2792.45; a `.nupkg` is a zip).
3. Run `build.bat` -> `build\MarkdownViewer.exe`.

`index.html` is an earlier browser-only version.

## Tests

`testsuild_test.bat` builds a CLI renderer; `python tests/run_spec.py` runs it against the CommonMark examples (download `spec.json` from https://spec.commonmark.org/0.31.2/spec.json into `tests/`).

## License

MIT, see [LICENSE](LICENSE).
