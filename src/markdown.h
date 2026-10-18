#pragma once
#include <string>

// Converts Markdown (UTF-8) to an HTML fragment (UTF-8).
// Raw HTML is escaped except for a small whitelist of harmless inline tags.
// Relative image paths are prefixed with imageBase (e.g. "https://md.local/").
std::string RenderMarkdown(const std::string& markdown, const std::string& imageBase);
