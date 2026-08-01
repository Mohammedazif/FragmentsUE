# Third-Party Licenses

FragmentsUE includes the third-party components listed below. Each is used under a
permissive licence that allows commercial and closed-source distribution; the only
obligation is that these notices travel with the plugin.

| Component | Licence | Used for |
|---|---|---|
| earcut.hpp | ISC | Triangulating polygonal B-rep faces |
| FlatBuffers | Apache 2.0 | Reading the `.frag` binary format |

---

## earcut.hpp

- Upstream: https://github.com/mapbox/earcut.hpp
- Location: `Source/FragmentsUE/Private/earcut.hpp`
- Modified: yes. The unused Delaunay refinement pass (`detail::Refiner` and
  `mapbox::refine`) has been removed. No change to triangulation behaviour.

```
ISC License

Copyright (c) 2015, Mapbox

Permission to use, copy, modify, and/or distribute this software for any purpose
with or without fee is hereby granted, provided that the above copyright notice
and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF
THIS SOFTWARE.
```

---

## FlatBuffers

- Upstream: https://github.com/google/flatbuffers
- Location: `ThirdParty/flatbuffers/include/`
- Copyright 2014 Google Inc.
- Modified: no. The headers are vendored unchanged.

Licensed under the Apache License, Version 2.0. The full licence text is in
`ThirdParty/flatbuffers/LICENSE`. You may not use these files except in compliance
with that licence; a copy is also available at:

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software distributed
under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
CONDITIONS OF ANY KIND, either express or implied.

`ThirdParty/generated/index_generated.h` is output produced by the FlatBuffers
compiler from `ThirdParty/index.fbs`. Generated code carries no additional
licence obligation from FlatBuffers itself.

---

## Note on the `.frag` format

The `.frag` format and the `index.fbs` schema originate with ThatOpen Components
(https://github.com/ThatOpen). The schema file in `ThirdParty/index.fbs` is a
format description, not code from this plugin. If you redistribute it, check the
current ThatOpen licence terms.
