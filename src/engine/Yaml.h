#pragma once

#include <juce_core/juce_core.h>

namespace galpon
{

/** A small YAML reader for hand-written configuration like feedpak manifests:
    block mappings and sequences (any nesting), flow sequences and mappings ([1, 2], {a: 1}),
    plain / 'single' / "double" quoted scalars, | and > block scalars, # comments,
    numbers, booleans and null. Anchors, tags and multiple documents aren't supported.
    Returns an object var; on a syntax problem, `error` explains it (line number included). */
juce::var parseYaml (const juce::String& text, juce::String& error);

} // namespace galpon
