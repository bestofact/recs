# recs — implementation critique

## Where the reflection usage is genuinely good

- **The `Descriptor<Info>` pattern** (descriptor.h) is the strongest idea in the codebase: partial specializations constrained on `requires(has_annotation(...))`, with `static constexpr Metadata k_metadata = make_metadata()` memoizing all the consteval work once per entity. Do the expensive walk once, escape consteval allocation via `std::define_static_array`, hand out spans.
- **`define_aggregate` for storage synthesis** (storage.h:93-151) is showcase-quality. Synthesizing the scene's data struct from the schema *and* sorting members by alignment/size to minimize padding is impossible without reflection. Same for the per-system `Query` aggregate (query.h:192).
- **The `ensure` diagnostic mechanism** (meta.h:358-378) — `reflect_invoke` into a template whose `static_assert(false, Message)` carries a `define_static_string`-formatted message with real type/function names — is a clever solution to "consteval errors are unreadable". The `static_assert((ensure_system(), true))` trick (descriptor.h:474) to force validation at instantiation is also nice.
- **Precomputing `m_invoke_template_arguments`** in the descriptor and using `std::meta::substitute` to stamp out `run_system<System, Params...>` (scene.h:130) moves template argument assembly to one place.
- **Annotations as the entire API surface** — signature-as-query and return-type-as-structural-op is a genuinely novel, coherent design. Compile-time cycle detection printing named paths (schedule.h:282-364) is better DX than most runtime ECS libraries.

## Where the reflection usage is weak

1. **`template for` where plain consteval loops would do — biggest compile-time sin.** `std::meta::info` is an ordinary value at consteval time; `template for` is only needed when each element must be a constant expression (splice / template argument). `Query::find_queried_component_index` (query.h:36-57), `Schedule`'s dependency collection, and several `ensure_*` loops instantiate per-element for no reason.
2. **Dependency analysis is O(N²) in template instantiations.** Each system gets a `Schedule<system>` class, each instantiating `is_depends_on<other>()` per other system, each instantiating `contains_sibling_of<C>`/`contains_descendant_of<C>` per component (schedule.h:28-166). For 50+ systems this will hurt. All access-set data is already plain `span<const info>` — the whole adjacency matrix could be computed in **one** consteval function with ordinary loops. Highest-leverage compile-time fix.
3. **Not dogfooding reflection.** `index.h`, `cursor.h`, `count.h` are three byte-identical 70-line strong-typedef structs.
4. **Dead metafunctions.** meta.h carries a combinatorial family (`is_const_pointer_to_const`, `is_mutable_pointer_to_const`, `is_const_rvalue_reference`, `is_const_value_type`, …) several of which are never used. `strip_type`'s trailing `return stripped;` (meta.h:263) is unreachable.
5. **Diagnostic polish.** Typos in user-facing errors ("mofidied", "writted", "Laslty"; "migh"/"tansient" in scene.h comments). Double-schema `ensure` at descriptor.h:30 has no message. A pointer-typed parameter falls through read/write/reject classification silently and surfaces as the cryptic "has mismatch type counts" instead of a named error.

## Is it a good, optimized ECS?

A great reflection showcase and a *situationally* good ECS — coherent and fast for fixed-capacity, dense, particle-like scenes (exactly the examples). As a general-purpose ECS the runtime model has real costs:

1. **Per-system query state multiplies memory and structural cost.** Every system owns `std::array<size_t, capacity> m_entity_query_indices` + `std::array<bitset, capacity>` + two vectors (query.h:176-181). 1M entities × 10 systems ≈ 80MB of indices alone. `set<Position>(i)` isn't O(1) — it fans out to every system that queries Position, plus parent/sibling cascade recursion.
2. **`view()` copies the whole entity vector when dirty** (query.h:110-118). One structural change → next frame copies a multi-MB vector per affected system. A deferred command buffer (apply set/reset at end of a system's pass) would remove both the copy and the buffer.
3. **Iteration order degrades.** Swap-remove (query.h:164) shuffles `m_entities`; over time iteration walks the component slab in random order — quietly defeating the README's "iteration walks the slab without indirection" claim. Periodic sort/compaction or bitset-scan iteration would restore cache-friendly traversal.
4. **`init()` and transient resets are brute-force**: capacity × components × interested-systems per-entity `reset` calls (scene.h:141-173). Bitsets could be bulk-initialized and matched sets rebuilt in one pass.
5. **Stages are computed and then thrown away.** All that dependency analysis packs systems into provably-independent stages… run sequentially (scene.h:121). A parallel stage executor is the natural payoff and the biggest unexploited runtime win. (Caveat: sibling-cascade `set` writes into other systems' queries — cross-query mutation must become deferred before parallelizing.)
6. **Missing entity lifecycle.** No create/destroy, free-list, or generation counters — index reuse and ABA are the user's problem.

Smaller items:
- `m_entity_query_indices` could be 4-byte `recs::index` instead of `size_t`.
- `m_entities` is never `reserve`d (repeated reallocation while 1M entities spawn).
- `scene::set` won't compile for a component no system queries (a legal no-op turned hard error).
- `const T*` return protocol: the pointed-to value is never used, only its nullness — the API implies data flow that doesn't exist.
- `operator unsigned int&() &` on `index` is a mutation footgun.

## Priority list

Compile time:
1. Replace pairwise `Schedule<system>` template machinery with one consteval scheduler function using plain loops.
2. Drop `template for` wherever a plain consteval loop suffices.
3. Named diagnostic for pointer params; fix typos; delete unused metafunctions; deduplicate index/cursor/count.

Runtime:
1. Deferred structural changes (command buffer) → kills the `view()` copy and unlocks parallelism.
2. Parallel stage executor.
3. Bulk `init()`/transient reset via direct bitset initialization.
4. Shrink query index arrays; keep entity lists compacted/sorted for in-order slab traversal.
5. Optional entity allocator (free list + generations).

**Bottom line:** as a learning project for static reflection this is well above average — annotations, `define_aggregate`, `define_static_array`, `substitute`, `reflect_invoke` all used in load-bearing, idiomatic ways. The weaknesses: (a) leaning on template instantiation where consteval evaluation would be cheaper, and (b) a runtime model whose per-system fan-out trades memory and structural-change cost for query speed without yet cashing in the parallelism that would justify it.

---

# tecs (C++17 port) — comparison

## Features tecs has that recs is missing

1. **`bool` return → early loop exit** (tecs scene.h:210, 182-187). A system returning `false` breaks the per-entity iteration for that system this frame. recs has no way to stop iterating.
2. **`T&&` return → unconditional `reset<T>`** (tecs descriptor.h:316-317, scene.h:237-242). Includes the scheduling homework: `T` is injected into `write_types` even without a `T&` parameter (descriptor.h:333-336) so read/reject-after-write edges are inferred, and a `T&` parameter is promoted back into `accept_types` (descriptor.h:341-345) since removal requires presence. recs only knows `void` / `const T&` / `const T*`.
3. **Entity lifecycle primitives**: `tecs::invalid_index` sentinel, `scene::next()` (first fully-empty slot, scene.h:147), `scene::free(index)` (reset every component, scene.h:160), backed by `is_entity_empty` / `has_no_present_components` (query.h:102-105). The `empty_mask` XOR trick — precompute the bit pattern of "nothing present" (accept bits off, reject bits on) so emptiness is one `^` + `.none()` — is clever.
4. **Graceful no-op for unqueried components.** `tecs::scene::set<C>` on a component no system queries does nothing; recs hard-fails the `requires(Query::is_queried(...))` clause on a legal call.

## Actual recs bugs exposed by the comparison

- **Swap-remove is out-of-bounds when removing the last matched entity.** `Query::reject` (recs query.h:164-169) swaps with `.back()`, `pop_back()`s, then unconditionally reads `m_entities[old_query_index]` — if the removed entity *was* the last element, that read is past the new end (UB). tecs guards it with `if (old_qi != m_entities.size() - 1)` (tecs query.h:141). Port back verbatim.
- **A system returning `T*` (pointer-to-mutable) is silently never invoked.** `ensure_system` accepts it (recs descriptor.h:376) but `run_system_for_entity` only branches on `void` / const-lref / pointer-to-*const* (recs scene.h:78-102) — `T*` falls through every `if constexpr` and the system body never runs. Handle it or reject it in validation.
- **tecs zero-initializes** bitsets, query indices, and storage (`{}` on all members); recs leaves `m_entity_query_indices` and storage arrays default-initialized (indeterminate). recs's logic happens to guard the garbage reads, but it's fragile.

## What tecs structurally does better

**The scheduler shape.** tecs builds a plain `constexpr` `array<array<bool,N>,N>` dependency matrix and runs Kahn's algorithm as one ordinary constexpr function (tecs schedule.h:338-404) — exactly the shape recommended above for recs. The C++17 port, forced to avoid fancy machinery, landed on the better compile-time architecture. recs with C++26 can go further than tecs: the whole matrix *including pair classification* can be one consteval function over the descriptors' spans, with zero per-pair template instantiations (tecs can't avoid `is_depends_on<A,B>` instantiations; recs can).

Also worth adopting (per tecs's own RETROSPECTIVE): an **explicit-schema registration mode** coexisting with the namespace walk — useful when a scene's contents span several headers and you want one line saying "this is what's in the scene."

## What recs still does better

- Cycle errors that print the named dependency path; tecs just says "cyclic dependency detected".
- Formatted diagnostics interpolating the offending type/function name; tecs has bare string literals.
- The `has_indirection_member` recursive layout check — impossible in C++17, and it enforces the library's core thesis.
- Alignment-sorted storage via `define_aggregate`; tecs is declaration-ordered `std::tuple`.
- Zero registration — tecs's schema list is a second source of truth the user maintains.

## Punch list to port back into recs

1. Swap-remove guard (bug).
2. Fix or reject `T*` returns (bug).
3. `bool` early-exit return.
4. `T&&` unconditional remove, with the write/accept scheduler integration.
5. `next()` / `free()` / `invalid_index` / `is_entity_empty` + empty-mask trick.
6. Value-initialized members.
7. No-op `set` for unqueried components.
8. Single-function consteval scheduler (matrix + Kahn's), keeping recs's named cycle diagnostics.
9. Optional explicit-schema registration mode alongside the namespace walk.
