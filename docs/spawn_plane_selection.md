# Spawn Plane Selection (Why lvl2 Spawn Was Hard, and How We Fixed It)

## Problem Summary

Our gameplay is essentially a **2.5D platformer**:

- The level is fully 3D.
- The player is simulated on a **single depth rail** (a fixed \(z\) plane).
- Every frame we enforce this by doing `player.pos.z = levelMidZ` in `Player::Update`.

So the entire question becomes:

> **Which \(z\) plane should we use for a given level so that the player spawns on the intended “main path”?**

## Why the “pick a platform collider” approach failed

The level collision data we currently expose from `Model` is:

- `colliderMins_` / `colliderMaxs_`
- **One AABB per triangle** (per-triangle AABBs), not one AABB per logical platform.

That means:

- The “ground” is not represented by one collider.
- It’s represented by **thousands of tiny AABBs**.
- Any attempt to “pick the widest platform” or “pick the lowest platform” on a given \(z\) plane is unreliable, because you’re effectively picking a **random triangle** that overlaps the plane.

This is why we kept spawning on the wrong piece (like the small side platform) or inside geometry.

## The technique we used: sample + vote (histogram)

We switched to a method that works well with per-triangle AABBs:

### 1) Query “is there ground here?” directly

We already had a helper:

- `LevelManager::FindGroundBelow(x, z, maxY)`

It answers:

> Given a point \((x,z)\), what’s the highest surface \(y\) below `maxY`?

So instead of picking a single collider, we **probe the world**.

### 2) Sample a grid near the start area

In `LevelManager::SpawnPlayerAtLevelStart`, we sample a small grid:

- several `x` positions near the left side
- many `z` positions across depth

For each sample \((x,z)\):

- compute `groundY = FindGroundBelow(x, z, maxYQuery)`
- keep it only if it looks like valid ground (not “no hit”)

### 3) Quantize heights and build a histogram

Many samples hit the *same* floor height (the main path).

We group heights into “bins” (quantization), e.g. 0.25 units per bin:

- `bin = floor(groundY / binSize)`

Then we count how many samples land in each bin.

### 4) Choose the “dominant” floor

We select the bin with the most samples.

Intuition:

- The main floor is large → it gets hit by many samples → it dominates.
- Small platforms are small → fewer samples → they don’t win the vote.

### 5) Pick a stable spawn point on that floor and lock the rail

From samples in the dominant bin:

- pick the **leftmost** X (start side)
- compute a Z for the “middle of the corridor” by averaging nearby Z samples at that leftmost X

Then we:

- set `gameplayPlaneZ_` and `levelMidZ_` to that Z
- spawn the player at:
  - `x = chosenX`
  - `y = groundY + playerHalfHeight + padding`
  - `z = chosenZ`

### 6) Why this works

This method is robust because it:

- Doesn’t assume “platform colliders exist”
- Works with per-triangle AABBs
- Uses the size of surfaces (area/coverage) as an implicit signal via vote count

## Where to look in the code

- **Depth lock**: `src/Player.cpp` (the `pos.z = levelMidZ` line)
- **Spawn sampling + histogram**: `src/LevelManager.cpp` in `LevelManager::SpawnPlayerAtLevelStart`
- **Ground queries**: `src/LevelManager.cpp` in `LevelManager::FindGroundBelow`

## Notes / Limitations

- This is a practical hack for a course project: effective, simple, and data-driven.
- A “proper” engine might instead:
  - generate simplified colliders (one per platform)
  - tag a spawn point in the level file
  - use a navigation mesh / walkable surface detection


