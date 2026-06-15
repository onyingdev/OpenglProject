# Input System

Two parallel input paths, both driven by data-bound **actions** (never raw key codes in game code):

- **Raw / per-frame** — `IsActionPressed`, `BindPressed`, mouse look. FPS-paced, zero latency. Use for
  movement, camera, aim — anything where you want the freshest state every frame.
- **Buffered / time-based** — `BindBuffered`, `ConsumeBuffered`, `Buffer()`. Frame-rate independent,
  event-driven capture, with a tunable per-action window. Use for discrete, timing-sensitive actions:
  jump-buffering, attacks, spawns, combos.

The two coexist and read the same physical key independently — you can even use both on one action.

---

## Configuring actions — `app/Config/input.json`

```json
{
  "bufferDefaultMs": 120,
  "actions": {
    "MoveForward": "W",
    "MoveLeft":    "A",
    "Jump":        { "key": "Space", "buffer": 150 },
    "Punch":       { "key": "J", "buffer": 200 }
  }
}
```

- An action maps to either a **key name string** (uses the default buffer window) or an object
  **`{ "key": ..., "buffer": <ms> }`** to override its buffer window.
- `bufferDefaultMs` is the fallback window (ms) for any action without its own `buffer`.
- Key names are case-insensitive: letters/digits, `Space`, `Escape`, `Enter`, `Tab`, arrows
  (`Up`/`Down`/`Left`/`Right`), `LeftShift`/`LeftCtrl`/`LeftAlt` (+ Right variants), `F1`–`F12`.
- The file **hot-reloads** — edit it while running and bindings + windows update live.

---

## Reaching the input system from a script

A script gets the per-object `InputComponent`, and through it the shared `InputManager`:

```cpp
InputComponent* in = owner->getComponent<InputComponent>();
InputManager*   im = in->GetManager();
```

Use `in` for the bind helpers (callbacks), `im` for direct queries (polling / buffer reads).

---

## Raw / per-frame API (snappy — movement, camera, mouse)

### Polling (`InputManager`)
```cpp
if (im->IsActionPressed("MoveForward")) pos += forward * speed * dt;  // true every frame held
if (im->IsActionReleased("ToggleColliders")) toggle();               // once, on the release edge
float dx = im->GetMouseOffsetX(), dy = im->GetMouseOffsetY();         // per-frame mouse delta
```

### Bind helpers (`InputComponent`) — handlers run in the component's `update(dt)`
```cpp
in->BindPressed("MoveForward", [this](float dt){ tf->Position += tf->forward() * Speed * dt; }); // every frame held
in->BindReleased("Quit",       [this](float)  { quit(); });                                       // once on release
in->BindMouseLook([this](float xOff, float yOff){ cam->addYawPitch(xOff, yOff); });               // per-frame mouse
```

`BindPressed` is the right tool for held movement — firing every frame *with `dt`* is correct there.

---

## Buffered / time-based API (frame-rate independent — discrete actions & combos)

Presses are captured event-driven and time-stamped the instant GLFW delivers them, then kept for the
action's window. Nothing is missed at low FPS, and timing is measured in real seconds.

### Bind helper (`InputComponent`)
```cpp
// Fires exactly once per physical press (consumes the buffered press). The window means a press made
// slightly early still counts.
in->BindBuffered("Jump", [this](float){ jump(); });
```

### Direct queries (`InputManager`) — for logic that must decide *when* to consume
```cpp
// Platformer jump buffer: check the moment the action becomes ALLOWED (e.g. on landing). A press up to
// the Jump window before touching ground still triggers.
if (grounded && im->ConsumeBuffered("Jump")) jump();

bool   want   = im->IsBuffered("Dash");          // peek without consuming
double ago    = im->TimeSincePressed("Dash");    // seconds since newest live press, or -1 if none
```

### Combos
```cpp
// "Punch then Kick within 200 ms" -> one combo. Consumes the matched presses on success.
if (im->ConsumeSequence({ "Punch", "Kick" }, 0.2)) doSpecial();

// Or read the raw timestamped history (oldest -> newest) and match however you like:
for (const BufferedInput& e : im->Buffer()) {
    // e.action, e.time (seconds), e.consumed
}
```

**Custom-timed sequences** — when the *gap between* inputs should pick the combo, use a
`CustomTimedSequence`: a list of `TimedStep`s (each with its own `[minGap, maxGap]` from the previous
press, in seconds — ignored on the first step) plus an interrupt policy.
```cpp
// A->B fast = X; A->B fast then a SLOW A = Y. Check the longer/more specific sequence FIRST so a shorter
// prefix doesn't consume the presses out from under it.
CustomTimedSequence Y { { {"A"}, {"B", 0.0, 0.15}, {"A", 0.3, 0.8} } };   // 3rd A must be slow
CustomTimedSequence X { { {"A"}, {"B", 0.0, 0.15} } };
if      (im->ConsumeSequence(Y)) doY();
else if (im->ConsumeSequence(X)) doX();
```
The `minGap` on Y's third step is what stops a *fast* third A from matching Y.

**Interrupt policy** — controls whether OTHER inputs landing between steps cancel an in-progress match:
- `SequenceInterrupt::None` (default) — interspersed inputs are ignored; the ordered actions + timing
  still trigger.
- `SequenceInterrupt::Any` — any action that isn't the next expected step cancels the match.
- `SequenceInterrupt::Except` — only the actions in `interrupters` cancel it; everything else is ignored.
```cpp
CustomTimedSequence fireball {
    { {"Down"}, {"Forward", 0.0, 0.2}, {"Punch", 0.0, 0.2} },
    SequenceInterrupt::Except, { "Block" }    // only Block cancels it mid-input; stray inputs don't
};
if (im->ConsumeSequence(fireball)) doFireball();
```
`ConsumeSequence` is a greedy matcher; for charge moves, motion inputs, or branching trees, read
`Buffer()` directly. (Sequence logic lives in scripts, not the engine.)

### Tuning at runtime
```cpp
im->SetDefaultBufferTime(0.10);       // 100 ms default
im->SetBufferTime("Jump", 0.18);      // per-action override (seconds)
```
(These are normally driven by `input.json`; call them only for runtime/dynamic tuning.)

---

## Which to use

| Need | Use |
|---|---|
| Movement, camera, hold-to-act | `IsActionPressed` / `BindPressed` (raw, per-frame, with `dt`) |
| Mouse look / aim | `BindMouseLook` / `GetMouseOffset*` (raw) |
| Discrete one-shot (jump, attack, spawn) | `BindBuffered`, or `ConsumeBuffered` when *you* control timing |
| "Buffered just before allowed" (jump buffer, coyote-style) | `ConsumeBuffered` at the allow-point |
| Combos / input sequences | `ConsumeSequence` or read `Buffer()` |
| Fire on key release | `BindReleased` / `IsActionReleased` |

---

## Example: `CubeSpawner`

```cpp
// assets/scripts/cubeSpawner.h — start()
in = owner->getComponent<InputComponent>();
im = in->GetManager();
// Once per press, frame-rate independent, honoring SpawnCube's 150 ms window. Tap = one cube; holding
// does NOT spam. (Want hold-to-spam for stress tests? swap to BindPressed.)
in->BindBuffered("SpawnCube", [this](float){ if (Enabled) spawn(); });
```

Correct as written. The only gotcha: if you want the old continuous hold-to-spam (for stress-testing
1000s of cubes), use `BindPressed` instead — `BindBuffered` deliberately fires once per press.

---

## Buffered state (timed gameplay states)

A separate but adjacent tool: `BufferedStateComponent` (`type: "BufferedState"`) holds named states that
**auto-revert to a default after a TTL** — invulnerability after a hit, a timed buff, a stance. Not part
of the InputManager; a per-entity component you read from scripts. Timers are `dt`-based (frame-rate
independent). Values are strings (covers flags *and* multi-value states); `SetFlag`/`IsFlag` are the
boolean shortcut.

```json
{ "type": "BufferedState",
  "states": [
    { "name": "invulnerable", "default": "false", "ttl": 0.5 },
    { "name": "buff",         "default": "none",  "ttl": 5.0 }
  ] }
```

```cpp
auto* st = owner->getComponent<BufferedStateComponent>();

st->SetFlag("invulnerable");            // "true" for the configured 0.5s, then auto-reverts to "false"
if (st->IsFlag("invulnerable")) return; // e.g. ignore damage while invulnerable

st->Set("buff", "strength", 5.0);       // reverts to "none" after 5s (ttl override; omit to use configured)
std::string b = st->Get("buff");        // "strength" ... then "none"
st->Reset("buff");                      // force back to default now
```

API: `Set(name,value[,ttl])` · `Get` · `Is(name,value)` · `Reset` · `SetFlag(name[,ttl])` · `IsFlag` ·
`Define(name,default,ttl)` (runtime-declared state). A `ttl` of 0 means "no auto-revert" (manual only);
setting a state back to its default clears the timer.
