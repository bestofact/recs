<div align="center">

<img width="2560" height="800" alt="recs-banner" src="https://github.com/user-attachments/assets/df7dff87-d812-42e9-ab3d-67b0636c3032" />

# Reflected Entity Component System

**A reflection-driven ECS for C++26.**

</div>

---

RECS is a header-only entity-component-system that uses C++26 static reflection to skip the registration and wiring steps most ECS libraries need. You declare your components, resources, and systems inside a namespace, and RECS reads that namespace at compile time to build the storage, the queries, and the run order. The library is opinionated: what you can express is what RECS can keep cache-friendly and statically schedulable, and what it refuses to compile tends to be what would have broken data-oriented design at runtime.

```cpp
#include "recs/scene.h"

namespace game
{
    // Components are flat aggregates. Tag them and forget about them.
    struct 
    [[= recs::component{}]] 
    Position 
    { 
      float x = 0.0f;
      float y = 0.0f;
    };
    struct 
    [[= recs::component{}]] 
    Velocity 
    { 
      float x = 0.0f;
      float y = 0.0f;
    };

    // Resources are singletons available to any system.
    struct 
    [[= recs::resource{}]]
    Clock 
    { 
      float dt = 0.0f;
    };

    // A system is just a free function. Its signature is its query:
    //   Position&         : the component this system writes to.
    //   const recs::index : index of the iterated entity.
    //   const Position&   : entity must have Position (read access).
    //   const Velocity&   : entity must have Velocity (read access).
    //   const Clock&      : inject the Clock resource, does not contribute to the query.
    // -> const Position&  : set the Position component on every iterated entity.
    [[= recs::system{}]]
    const Position& update_position(
        Position& out,
        const recs::index i,
        const Position& p,
        const Velocity& v,
        const Clock& c)
    {
        out.x = p.x + v.x * c.dt * static_cast<float>(i);
        out.y = p.y + v.y * c.dt * static_cast<float>(i);
        return out;
    }

    // The schema marks the namespace as a RECS scene.
    // Required parameters for a RECS scene is passed via the schema annotation.
    // With this schema, RECS will consider every annotated type under the parent namespace as a RECS type.
    struct 
    [[= recs::schema{.entity_capacity = 1024}]] 
    Schema 
    {};
}

int main()
{
  // Simply define your scene, init and run.
  recs::scene<^^game::Schema> scene;
  scene.init();
  scene.run();
}
```

The namespace is the entire spec. Nothing about the storage, the schedule, or the queries is written anywhere else. RECS reads the components, resources, and systems at compile time and emits the storage layout and the run order from what it finds. There is no second source of truth to keep in sync.

---

## Getting RECS

RECS itself is header-only and has no dependencies. Drop `include/recs` into your project and `#include "recs/scene.h"` to use the library. The catch is that *compiling* code that uses it needs a C++26 toolchain with [P2996] static reflection, which no shipping compiler supports yet. The repo ships an xmake task that provisions [Bloomberg's clang-p2996 fork][cp] into a local `.toolchains/` folder so you don't have to track it down or install it system-wide.

```sh
xmake setup-clang-p2996    # clone + build + install the toolchain (one-time)
xmake test                 # build and run the compile-smoke
```

The `test` target turns on automatically once the toolchain is on disk.

[P2996]: https://wg21.link/P2996
[cp]: https://github.com/bloomberg/clang-p2996

---

## How RECS differs

RECS uses C++26 reflection to do at compile time what most ECS libraries handle at runtime. A few things follow from that:

- **The system signature is the query.** Parameters declare both access (`const T&`, `T&`) and exclusion (`T&&`). The function's parameter list is the only place a query lives, and the engine never dispatches a system on an entity that doesn't match.
- **The return type is the intended way to add or remove a component.** `void` leaves the entity unchanged, `const T&` sets `T`, `const T*` sets `T` when non-null and resets it when null. RECS doesn't ship a `commands` or `world` argument; the return is what the scheduler reads when deciding who depends on whom.
- **The schedule is inferred from signatures.** Read-after-write on a component, sibling exclusion through component hierarchies, and group enum order combine into a DAG at compile time, and the build fails by name if it cycles. `[[=recs::after{^^other}]]` and `[[=recs::before{^^other}]]` exist as escape hatches for cycles the inference can't resolve.
- **Component hierarchies model archetypes.** Nesting `Plant`, `Herbivore`, and `Predator` inside a `Species` component means `set<Species::Plant>(i)` asserts `Species` and resets the other two siblings. Two systems that filter on different siblings are treated as disjoint, with no dependency edge between them.
- **Per-system bitsets, not a central table.** Each system owns a small bitset over the components it queries. Add and remove are O(1) and only touch the systems that asked about the changed component. There is no archetype migration on a flip and no "who has what" lookup on a query.
- **Static dispatch end to end.** `get<T>`, system invocation, and storage lookup are resolved at compile time. No virtual calls, no type erasure, no runtime registry. Empty systems cost nothing.
- **Misuse turns into a compile error.** A component holding a `std::vector`, a system with two writes to the same type, a cyclic schedule. The diagnostic names the offending type or pair, so there is no "forgot to register X" surprise at runtime.

---

## Concepts

### `recs::index`

The integer that names an entity. RECS entities are not objects: there's nothing to construct or destroy, just a stable index inside the schema's capacity. Ask for one by putting `recs::index` in your system's parameter list.
An index isn't a component and exists for every entity, so it doesn't contribute to the query. RECS hands the iterated index to any parameter typed `recs::index`.

```cpp
[[= recs::system{}]]
void log(const Position& p, const recs::index i) { /* ... */ }
```

### `recs::component`

A flat aggregate annotated with `[[= recs::component{}]]`. RECS enforces the data-oriented shape at the annotation site, with a named diagnostic per rule:

| Rule                                  | Why                                                                     |
| ------------------------------------- | ----------------------------------------------------------------------- |
| Must be an aggregate                  | Storage is a contiguous slab, with no constructor to call               |
| Must be trivially copyable            | Copy is `memcpy`; no copy/move/destructor traps in hot loops            |
| No base classes                       | Layout is exactly the declared fields, in the declared order            |
| No user-declared member functions     | Behavior lives in systems, not on the data                              |
| No pointer / reference / nested-indirection members | Storage is self-contained; no hidden lifetime, no aliasing surprise |

Empty components collapse to a shared instance and cost no storage, which is what makes them work as tags. The same rules apply to nested components; the layout discipline is the whole point.

```cpp
// component (storage)
struct [[= recs::component{}]] Velocity { float x, y; };
// tag (zero storage).
struct [[= recs::component{}]] Plant {};
// if transient is set to true, the component is reset after each run.
struct [[= recs::component{.transient = true}]] Dying {};
```

**Hierarchies.** Nest a component inside another and RECS treats them as parent/children. 
```cpp
struct [[= recs::component{}]] Character
{
    struct [[= recs::component{}]] Player 
    {
        int id = 0;
    };
    struct [[= recs::component{}]] NPC 
    {
      struct [[= recs::component{}]] Minion {};
      struct [[= recs::component{}]] Boss 
      {
        int level = 0;
      };
    };
};
```
Nesting doesn't create an inheritance-style relationship. Each component still owns its own storage and doesn't inherit any data from its parent or siblings. The hierarchy only declares the set/reset behavior and the disjointness the scheduler relies on.

Three things follow from this:

- `set<Child>(i)` automatically asserts the parent on the same entity and resets every sibling. Flipping a slot from herbivore to predator is one call.
- `reset<Parent>(i)` cascades to every descendant. Tearing down an entity by clearing its top tag is enough.
- In the schedule, two systems that each filter on different siblings are treated as disjoint. They cannot share an entity, so no dependency is ever inferred between them.


### `recs::resource`

A singleton, one per scene, injected into any system that names it. Use it for time, configuration, input, spatial grids, and anything else that doesn't live per-entity. Resources must be class types with at least one data member; empty resources, member functions on the resource, and base classes are rejected at compile time.

```cpp
struct [[= recs::resource{}]] Input { float mouse_x, mouse_y; bool down; };
```

### `recs::system`

A free function annotated with `[[= recs::system{}]]`. The function runs once per matching entity; the engine handles the iteration. The signature alone tells RECS three things: which entities the function may visit, what it may read or write, and how the function fits into the schedule. RECS doesn't pass in a `world` or `commands` argument by default, so the body normally operates inside those declared edges.

**Parameters express the query.**

| Parameter             | Meaning                                                                                        |
| --------------------- | ---------------------------------------------------------------------------------------------- |
| `const T&` (component)| read access to `T`; entity must **have** `T`                                                   |
| `T&` (component)      | write access to `T`; entity must **have** `T` unless `T` is the system's modified type         |
| `T&&` (component)     | entity must **lack** `T`. The parameter is for the filter only; don't name or use it           |
| `const R&` (resource) | read access to resource `R`; does not contribute to the query                                  |
| `R&` (resource)       | write access to resource `R`; does not contribute to the query                                 |
| `recs::index`         | the current entity's index; does not contribute to the query                                   |

Every system must filter on at least one component. Pure resource-only or pure index-only systems are rejected at compile time. Duplicate read or duplicate write entries are also rejected.

**The return type is the only way to add or remove a component.**

| Return     | Effect                                                                       |
| ---------- | ---------------------------------------------------------------------------- |
| `void`     | structural state of the entity is left alone                                 |
| `const T&` | unconditionally `set<T>` on the iterated entity                              |
| `const T*` | non-null `set<T>`, null `reset<T>`                                           |

If the return type names a component, the system must also have a writable parameter to it (`T&` in the signature). Together this means each system body normally flips the presence of *at most one* component per entity. There is no built-in API for "remove A and add B in the same pass"; that's two systems.

The rule is deliberate. A system is a free function with no registration step, so splitting one system into two costs nothing more than typing another function header. The payoff is granularity. Each system reads a small bitset, mutates one bit, and exposes one read/write edge to the scheduler. Monolithic "do five things to this entity" functions never appear, and dependency graphs stay sparse.

**Escape hatch.** Nothing physically prevents a system from calling `scene.set` or `scene.reset` directly: put a `recs::scene<...>*` (or reference) inside a resource and name that resource in the system's parameter list. The body can then flip any component on any entity. RECS leaves this open intentionally, mostly for cross-scene plumbing and similar edge cases, but it bypasses the scheduler. Changes made this way are invisible to the analyzer, so the inferred run order no longer reflects the real read/write graph. Use it sparingly and prefer the return type for normal entity work.

**Groups place a system in a phase.** The optional enumerator passed to `system{}` (or the schema's default group) pins the system to a stage; cross-group order is fixed by enum order, and no read/write inference runs across the boundary:

```cpp
// Position& : Write access to position component. Doesn't contribute to query since system sets the Position component.
// Position&&: Entity must lack Position component.
// const recs::index: Index of iterated entity.
// -> const Position& : System will set the Position component once it finishes.
[[= recs::system{^^Group::Init}]]
const Position& seed(Position& out, Position&&, const recs::index i)
{
    out.x = static_cast<float>(i);
    out.y = 0.0f;
    return out;
}

// No explicit group. Guaranteed to run after Init group.
[[= recs::system{^^Group::Update}]]
const Position& integrate(Position& out, const Position& p, const Velocity& v, const Clock& c)
{
    out.x = p.x + v.x * c.dt;
    out.y = p.y + v.y * c.dt;
    return out;
}
```

### `recs::after` and `recs::before`

Annotations that pin the order between two systems in the same group. They override whatever the analyzer would infer for that specific pair, leaving the rest of the schedule alone. Both take the reflection of the other system (`^^other_system`) and only apply within the same group; cross-group order is always decided by the group enum.

```cpp
[[= recs::system{^^Group::Update}, = recs::after{^^integrate}]]
void log_position(const Position& p, const recs::index i) { /* ... */ }

[[= recs::system{^^Group::Update}, = recs::before{^^integrate}]]
const Velocity& damp_velocity(Velocity& out, const Velocity& v) { /* ... */ }
```

These are escape hatches, not the default tool. Reach for them only when two systems write each other's reads (a true cycle the analyzer cannot resolve), or when you need a deterministic order between two systems whose signatures don't imply one. Sprinkling `after`/`before` to nudge the schedule by hand defeats the inference and makes the dependency story harder to read.

### `recs::schema`

The marker that turns a namespace into a scene. It's an empty type, annotated with the entity capacity. RECS walks the schema's parent namespace at compile time, finds everything tagged as `component`, `resource`, or `system`, and synthesises a scene type from them. Only one schema per namespace is allowed, and a component, resource, or system belongs to the schema in its enclosing namespace.

```cpp
struct [[= recs::schema{.entity_capacity = 65'536}]] Schema {};
```

The schema also optionally names a group enum and a default group. When present, every system either picks its own group or lands in the default, and cross-group ordering is fixed by the enumerator order:

```cpp
enum class Group : size_t { Reset, Prepare, Update, Kill, Respawn, Render };

struct [[= recs::schema{
    .entity_capacity = 65'536,
    .group_enum = ^^Group,
    .default_group = ^^Group::Update
}]] Schema {};
```

The schema itself has no body. It just declares that the enclosing namespace is a scene, with the given capacity and phases.

### `recs::scene`

The runtime instance. `recs::scene` is a class template parameterized by the reflection of the schema; instantiating it produces a concrete type whose layout, query bitsets, and scheduled run order all come from the schema's namespace.

```cpp
recs::scene<^^game::Schema> scene;
```

The scene is default-constructible. There is no builder, no registration step, and no setup callback. The storage and query arrays are sized at compile time from the schema's `entity_capacity`, so the scene is typically large; if you have a strict stack budget, heap-allocate it (`std::make_unique<recs::scene<^^game::Schema>>()`).

The public API is small:

| Method                          | Purpose                                                                                                          |
| ------------------------------- | ---------------------------------------------------------------------------------------------------------------- |
| `init()`                        | Resets every component on every entity. Call once before the first `run()`.                                      |
| `run()`                         | Runs one tick: every system in scheduled order, then resets every transient component.                           |
| `set<C>(entity)`                | Marks component `C` as present on `entity`. Cascades to the parent and resets siblings if `C` is in a hierarchy. |
| `reset<C>(entity)`              | Marks component `C` as absent on `entity`. Cascades to every descendant.                                         |
| `get<T>(entity = 0)`            | Returns a reference to component or resource `T`. The index is ignored for resources.                            |

A typical lifecycle:

```cpp
recs::scene<^^game::Schema> scene;
scene.init();

// Seed some entities.
scene.set<game::Position>(0);
scene.set<game::Velocity>(0);
scene.get<game::Position>(0) = {.x = 10.0f, .y = 0.0f};

// One tick.
scene.run();

// Read back.
const game::Position& position = scene.get<game::Position>(0);
```

`init()` must run before any `set`/`reset`/`get` calls, because the component bitsets are uninitialized on a fresh scene. After that, `run()` may be called as many times as you want; transient components reset at the end of each tick so they only live for one frame.

---

## How the schedule gets built

RECS classifies each pair of systems using the following rules. They are checked in order, and the first rule that matches decides the relationship for that pair. Pairs that no rule pins down are free to run in either order.

1. **Different groups: the enum decides.** Systems in different groups follow the order of the group enum. No read/write inference runs across a group boundary, so a `Group` enum on the schema collapses most ordering decisions into a single declaration.

2. **Same group, manual edge: the edge wins.** An `[[=recs::after{^^other}]]` or `[[=recs::before{^^other}]]` annotation on a function overrides anything the analyzer would infer for that pair. Useful when two systems write each other's reads, such as a position/velocity update pair.

3. **Sibling components: disjoint.** If A filters on a child of `Species` (say `Species::Plant`) and B filters on a different child (`Species::Herbivore`), the two cannot share an entity, because setting one sibling resets the others. The analyzer skips dependency analysis between them.

4. **Reject vs. accept without a write: disjoint.** If A rejects `T` (via `T&&`) and B accepts `T` (via `T&` or `const T&`) but A doesn't write `T`, the two systems target disjoint entity sets this tick. The "without a write" caveat matters: a system that flips `T` from absent to present still needs to be ordered against systems that read `T`, even if it itself rejects `T` on entry.

5. **Read-after-write: ordered.** If B reads anything A writes, B depends on A. Most of the schedule comes from this rule.

6. **Reject-after-write: ordered.** If B rejects `T` and A writes `T`, B depends on A. A may add `T` to entities, which would change who B sees.

Once every pair is classified, RECS performs a topological sort. If the graph is acyclic, the systems get packed into stages, with each stage holding everything whose dependencies were satisfied by earlier stages. Systems inside a stage have no edges between them. If the graph cycles, the build fails and the error lists each cycle path, with arrows read as "depends on".

In practice, declaring the phases your scene actually has, nesting archetype tags under a parent component, and keeping systems small is enough to get most of the schedule decided for you.

---

## Showcases

Built with `xmake f --examples=y`.

![recs_led](https://github.com/user-attachments/assets/e91b8260-3d4f-4a57-83d8-419159961b06)
**recs_led.** ~300k particles assembling into the word "RECS.".

![recs_game](https://github.com/user-attachments/assets/58b2178e-064b-45d7-bcee-fdd093e9c069)
**recs_game.** A tiny twin-stick demo (Player + NPC + attacks). 

![recs_balls](https://github.com/user-attachments/assets/2ca31776-71e0-44cc-9d06-d48a9144dd23)
**recs_balls.** A million bouncing circles.

![recs wild](https://github.com/user-attachments/assets/828829fc-5ee3-4ecf-a94e-cf0b20812bf4)
**recs_wild.** Plants regrow and seed, herbivores graze and flee, predators hunt.
## License

MIT. See [LICENSE](LICENSE).
