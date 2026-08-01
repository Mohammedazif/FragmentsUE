# FragmentsUE

Import **ThatOpen Fragments 2.0** (`.frag`) BIM models into **Unreal Engine 5** — with the full IFC data, not just the geometry.

Every imported element keeps its identity: GlobalId, IFC class, name, type, building storey, attributes, property sets, quantities, materials and classifications. Click a wall in Unreal and you see the same information you would see in the source IFC.

![FragmentsUE](https://img.shields.io/badge/UE-5.6-blue) ![Status](https://img.shields.io/badge/status-beta-orange)

---

## What is Fragments?

[Fragments](https://github.com/ThatOpen/engine_fragment) is an open, compact binary format for BIM models, developed by [ThatOpen](https://thatopen.com/) (the people behind IFC.js). You convert an IFC file to `.frag` once — with ThatOpen's tools — and get a file that is a fraction of the size and loads in seconds instead of minutes.

This plugin reads `.frag` files natively in Unreal. No Datasmith, no IFC parser at runtime, no external programs.

---

## Features

- **Drag-and-drop import** — drop a `.frag` file into the Content Browser, then drag the created asset into the viewport
- **Full IFC metadata** — attributes, property sets, quantity sets, materials with layer thicknesses, classifications (Uniclass/OmniClass), type objects, spatial containment
- **Spatial hierarchy** — Project → Site → Building → Storey → Element actor tree in the World Outliner, matching what Datasmith would build
- **Details panel integration** — select any element and read its IFC data in a dedicated panel, with clipboard copy
- **Element picking at runtime** — line trace against the model and get the element's full metadata from the hit
- **Filtering** — isolate a level or an IFC category from the built-in *IFC Filter* editor panel, or from Blueprint (`IsolateByStorey`, `IsolateByCategory`, `IsolateByAttribute`, ...)
- **Six import modes** — from one-actor-per-body (fully selectable) to whole-model merge (fewest draw calls)
- **Asset baking** — meshes and materials are saved to the Content Browser, so the level can be saved, cooked and packaged
- **Original IFC colors** — base color, translucency and glass are resolved into auto-generated materials; collision is query-only, so a fly-through camera never gets stuck
- **Blueprint API throughout** — every query and filter call is Blueprint-callable; build your own UI without touching C++

---

## Requirements

| | |
|---|---|
| Unreal Engine | 5.6 (built and tested); nearby 5.x versions likely work with a rebuild |
| Platform | Windows (tested); code is platform-independent |
| Input format | Fragments **2.0** `.frag` files |
| Dependencies | ProceduralMeshComponent (engine plugin, enabled automatically) |

To create `.frag` files from IFC, use ThatOpen's converter — see the [Fragments repository](https://github.com/ThatOpen/engine_fragment).

---

## Installation

1. Copy the `FragmentsUE` folder into your project's `Plugins/` directory:

   ```
   YourProject/
   └── Plugins/
       └── FragmentsUE/
   ```

2. Regenerate project files (right-click the `.uproject` → *Generate Visual Studio project files*) and build, or just open the project — the editor offers to compile the plugin.

3. On first launch the plugin creates its three base materials (`M_FragBase`, `M_FragBase_Translucent`, `M_FragBase_Glass`) in its own content folder. Nothing else to set up.

> **Optional but recommended:** enable **Project Settings → Physics → Support UV From Hit Results**. The merged import modes need it to identify which element a mouse click hit. The plugin logs a warning at import time if it is off.

---

## Quick start

1. **Drag a `.frag` file** from Windows Explorer into the Content Browser. This creates a Fragments Model asset (a lightweight pointer to the file — the heavy work happens later).
2. Select the asset and choose your **Import Options** in the Details panel (import mode, metadata on/off, asset path). The defaults are sensible.
3. **Drag the asset into the viewport.** The model is parsed, meshes and materials are baked into the Content Browser, and the actor hierarchy is built.
4. **Click any element.** The Details panel shows its IFC data: class, GlobalId, attributes, property sets, materials, storey.
5. Open **Window → IFC Filter** to isolate levels or categories.

That's the entire workflow. A multi-storey building imports in seconds.

---

## Import modes

The import mode decides how the model becomes actors — and therefore what you can select and filter later. Set it in the asset's Import Options before dragging into the viewport.

| Mode | Structure | Selection | Filtering | Best for |
|---|---|---|---|---|
| **Hierarchy – Actor per Body** *(default)* | Full spatial tree, one actor per body | Every body | Per element | Editing, inspection |
| **Hierarchy – Actor per Element** | Full spatial tree, one actor per element | Per element | Per element | The usual sweet spot |
| **Hierarchy – Merged per Storey** | Full spatial tree, one merged mesh per storey | Storey only | Per level | Large models, per-level control |
| **Instanced** | One actor, GPU-instanced meshes | Via trace only | — | Maximum performance, repeated geometry |
| **Procedural Mesh** | One actor, procedural meshes | Via trace only | — | Runtime-generated content |
| **Merged Whole Model** | One mesh per material | — | — | Background context models |

Rule of thumb: if you want to click and filter elements, use one of the first two. If you want raw framerate for a fly-through, merge.

---

## Reading IFC data

### In the editor

Select any imported actor. The **IFC Metadata** section in the Details panel shows the element's class, GlobalId, name, type, storey, every attribute, every property set (including ones inherited from the IFC type, marked as such), materials with layer thicknesses, and classifications. Values can be copied to the clipboard.

Selecting the top-level Fragments actor shows the model header instead: IFC schema, authoring tool, project/site/building names and units.

### From Blueprint — click an element at runtime

```
LineTraceByChannel (Visibility)
  └─ Hit → FragmentsActor → GetMetadataFromHit
       └─ FFragItemMetadata: GUID, Category, Name, Attributes, PropertySets, ...
```

`GetMetadataFromHit` resolves every import mode — per-element actors, instanced meshes and merged meshes alike. For merged meshes it needs *Support UV From Hit Results* (see Installation).

Useful calls on the metadata component / actor:

| Function | Returns |
|---|---|
| `GetActorMetadata` (library) | Full record for a clicked actor |
| `GetAllValuesFlat` | Everything as a `Map<String,String>`, ready for a UI list |
| `GetProperty("Pset_WallCommon", "FireRating")` | One property value |
| `GetPropertySetNames` / `GetPropertiesInSet` | Browse the property sets |
| `GetStoreyName`, `GetTypeName`, `GetMaterialNames` | Common one-liners |

### Model-level queries

On the Fragments actor:

| Function | Returns |
|---|---|
| `GetMetadataByGuid` / `GetMetadataByLocalId` | One element's record |
| `FindItemsByCategory("IFCDOOR")` | All doors |
| `FindItemsByStorey("Level 2")` | Everything on a level |
| `FindItemsByAttribute("FireRating", "60")` | Search across attributes and properties |
| `GetCategoryCounts` / `GetStoreyCounts` | What's in the model, with counts |

---

## Filtering

### Editor panel

**Window → IFC Filter.** Pick a model (if the level has several), then:

- **Levels list** — check/uncheck to show/hide a storey, or **Isolate** to show only that storey
- **Categories list** — the same per IFC class, sorted by element count
- **Show All** — clear the filter

### From Blueprint

All on the Fragments actor, under *FragmentsUE | Filter*:

```
IsolateByStorey("Level 2")          — show one level
IsolateByCategory("IFCWALL")        — show one class
IsolateByAttribute("Status", "New") — show by property value
SetCategoryVisible("IFCDOOR", false)— hide doors, keep the rest of the filter
ClearFilter()                       — bring everything back
IsFilterActive / GetHiddenCount     — filter state, for UI
```

This makes a runtime filter UI a pure UMG exercise: populate a dropdown from `GetStoreyCounts`, call `IsolateByStorey` on selection. No C++ needed.

> Filtering hides actors, so it follows the import mode: per-body and per-element modes filter individual elements, merged-per-storey filters whole levels, and the non-hierarchy modes have nothing to hide. `SupportsElementFiltering` tells you at runtime what the model can do.

---

## Console commands

| Command | Does |
|---|---|
| `FragmentsUE.Parse <filepath>` | Parse a `.frag` file and log statistics without spawning anything |
| `FragmentsUE.Import <filepath>` | Parse and spawn the model in the current world |
| `FragmentsUE.GenerateMaterial` | Rebuild the three base materials, discarding any edits to them |

---

## Programmatic import (C++ / Blueprint)

The editor drag-and-drop path is a convenience wrapper around an API you can call yourself:

```cpp
UFragmentsUESubsystem* Subsystem = GEngine->GetEngineSubsystem<UFragmentsUESubsystem>();

FFragImportOptions Options;
Options.ImportMode = EFragImportMode::HierarchyPerElement;

FFragImportResult Result = Subsystem->LoadFragFile(TEXT("C:/models/building.frag"), Options);

AFragmentsActor* Actor = Subsystem->SpawnFragmentsActor(World, Result, Options, BaseMaterial);
```

`LoadFragFile` returns an engine-agnostic parse result (geometry, instances, spatial tree, all metadata); `BuildFromImportResult` / `SpawnFragmentsActor` turns it into actors. Both steps are exposed to Blueprint.

Key import options:

| Option | Default | |
|---|---|---|
| `ImportMode` | Hierarchy – Actor per Body | See table above |
| `ScaleFactor` | 100 | Fragments is meters, UE is centimeters |
| `bImportMetadata` | on | IFC attributes, relations, GUIDs |
| `bImportPropertySets` | on | Property and quantity sets |
| `bAttachMetadataComponents` | on | Details-panel data on every actor |
| `bSaveAsAssets` | on | Bake meshes/materials to the Content Browser |
| `AssetPath` | `/Game/Fragments` | Where baked assets go |
| `bEnableElementPicking` | on | Query-only trace collision (never blocks movement) |

---

## How it works

```
.frag file (FlatBuffers)
   │
   ▼
FFragParser ──────────► FFragImportResult          (engine-agnostic: geometry in UE space,
   │                                                instances, spatial tree, all IFC metadata)
   ▼
AFragmentsActor::BuildFromImportResult
   ├─ FFragMeshBuilder   → static meshes (earcut triangulation, normals, vertex colors)
   ├─ FFragAssetFactory  → baked assets in the Content Browser
   └─ Spatial walk       → AFragmentsNodeActor (storeys, buildings)
                           AFragmentsElementActor (elements, with UFragmentsMetadataComponent)
```

- Coordinates are converted from Fragments (meters, right-handed) to Unreal (centimeters, left-handed Z-up) at parse time.
- Meshes are cached per geometry+material pair, so a window that appears 200 times is built once.
- Materials are auto-generated instances of three base materials (opaque, translucent, glass) driven by the colors in the `.frag` file.
- Collision is **query-only**: traces can identify elements, but nothing ever blocks a camera or character.

---

## Troubleshooting

**Clicking a merged model returns no metadata.**
Enable *Project Settings → Physics → Support UV From Hit Results*. The plugin logs this exact warning at import.

**The Details panel shows no IFC data.**
Metadata import was off for that model (`bImportMetadata` / `bAttachMetadataComponents`), or the `.frag` file was exported without properties. Try `FragmentsUE.Parse` to see what the file contains.

**The IFC Filter panel says there are no actors to hide.**
The model was imported in a non-hierarchy mode (Instanced / Procedural / Merged Whole Model). Re-import in one of the Hierarchy modes.

**Import stops with an actor-limit error.**
Very large models can exceed the per-body actor ceiling (250,000). Use *Actor per Element* or *Merged per Storey* instead.

**I edited M_FragBase and want it back.**
Run `FragmentsUE.GenerateMaterial`. (The plugin never overwrites your edits on its own.)

**Old levels imported before filtering existed.**
Filtering works there too — the filter index is rebuilt automatically from the saved actors.

---

## Third-party components

| Component | License | Used for |
|---|---|---|
| [earcut.hpp](https://github.com/mapbox/earcut.hpp) | ISC | Triangulating polygonal faces |
| [FlatBuffers](https://github.com/google/flatbuffers) | Apache 2.0 | Reading the `.frag` binary format |

Full license texts: [`ThirdParty/LICENSES.md`](ThirdParty/LICENSES.md). Fragments is an open format by [ThatOpen](https://thatopen.com/); this plugin is an independent implementation and is not affiliated with ThatOpen.

---

## License

MIT — see [LICENSE](LICENSE). Copyright (c) 2026 Mohammed Azif.

Free to use, modify and ship in commercial and personal projects.

---

Found a bug or need a feature? Open an issue.
