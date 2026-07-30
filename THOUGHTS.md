Pivot note: this plan supersedes the earlier Swift 6.3 + SDL3 direction. Bielik2D is now
targeting C23 — a clean rebuild that treats Cute Framework as donor code rather than a
Swift-native wrapper around SDL3. Everything below was written with that C23 target in
mind.

What Cute Framework actually is in mid-2026

The first-party surface is smaller than it looks: ~21.4k lines of implementation across 32 .cpp files (it's a C API over a C++20 codebase, per its own AGENTS.md), plus ~24.5k lines of public headers that are heavily doc comments. The rest of the repo weight is embedded data (a 103k-line calibri.h font header, a 15k-line joypad mapping DB) and vendored single-file libs: cute_sound + stb_vorbis (audio), cute_c2 + pico_qt (collision/spatial), cute_net/cute_tls/cute_https + s2n (networking), cute_aseprite, cute_png, minicoro, yyjson, stb_truetype, Dear ImGui. It FetchContents SDL 3.4.0 and PhysFS from source. The ECS is gone from src entirely.

Two findings matter most for your plan:

CF already made two of your choices. It uses PhysFS for its VFS and minicoro for coroutines. Those bullets on your list are validations, not departures.
CF maintains two graphics backends behind its API: SDL_GPU (~2.4k lines) and a GLES3 backend (~2k lines) that exists specifically for Emscripten/WebGL2 — plus its own shader toolchain (cute_shaderc + cute_spirv, a GLSL→SPIRV→HLSL transpiler they wrote to avoid shipping dxcompiler). The draw layer (the SDF batcher you like) is 5.3k lines on top.

So the honest decomposition: audio, collision, networking, image loading, JSON, coroutines, VFS are all replaceable with maintained off-the-shelf libraries. The irreducible custom core — app glue, graphics wrapper, SDF draw layer, text, asset plumbing — is roughly 12–15k lines. That's the real size of what you'd own in C23. With LLM leverage that's tractable, but note what LLMs don't reduce: debugging GPU driver quirks, audio device edge cases, and Emscripten weirdness on real hardware. That's where framework time actually goes.

Fork vs. rebuild: since CF's internals are C++, C23 means a rewrite regardless. But it's dual-licensed zlib/public-domain, so the right strategy is a clean rebuild that treats CF as donor code — transliterate the SDF shaders, the draw API semantics, and possibly the GLES backend rather than reinventing them.

Decide the web question first, because it shapes everything

SDL_GPU still has no web path. The community WebGPU backend PR was abandoned after the SDL team decided the feature needs to be rebuilt from the ground up due to API conflicts, and there's no shipped alternative in mainline SDL. CF's workaround is exactly that second GLES3 backend. Your options: 
GitHub

(a) Do what CF does: your own thin graphics API with SDL_GPU and GLES3 implementations. Cost: ~2k extra lines, a shader permutation matrix, and a permanent two-backend testing tax.
(b) Native-first: build purely on SDL_GPU, structure the graphics layer so a second backend can be added, ship web later when SDL ships WebGPU support (no ETA).
(c) sokol_gfx instead of SDL_GPU: the only single-API route to web today, but it violates your stated requirement and you lose the SDL_GPU ecosystem.

I'd push you to interrogate how much web actually matters. If browser playtests on itch are part of your loop, that argues for (a) and you should budget for it explicitly. If web is a "nice eventually," take (b) — it's the single biggest streamlining you can make relative to CF.

Picks for your requirement list
Audio: two defensible answers now. SDL3_mixer is a ground-up redesign of SDL2_mixer with a completely new API, supporting multiple devices and mixing to memory buffers, and requires SDL 3.4.0+; it finally shipped stable in March 2026 and is on 3.4.x. It's the all-SDL choice but only months old as a stable release. miniaudio is the battle-hardened alternative: zero dependencies, node-graph DSP, spatialization, proven wasm support. My lean is miniaudio for a framework core, SDL3_mixer if you value stack coherence and accept early-adopter risk. Skip MojoAL — it exists to provide OpenAL API compatibility, which is indirection you don't need. 
GitHub
Lazy Foo' Productions
Collisions/physics: Box2D v3 is the clear call — rewritten in C with SIMD and multithreading, currently at v3.1.1 with 3.2 in progress. It also just grew a sibling: Box3D, released as open source in June 2026, is effectively a fork of Box2D extended to 3D with triangle mesh and height-field collision, sharing largely the same data structures and APIs — which gives your "optional 3D later" requirement a coherent physics story instead of a bolt-on. If you only want overlap tests, cute_c2 is standalone and vendorable, but I'd default to Box2D and use sensors/queries. 
GitHub
Box2d
VFS: PhysFS, confirmed by CF's own usage. Write one PhysFS→SDL_IOStream bridge and route all loaders through it; archives-as-mounts is also the backbone of a data-driven asset pack story.
glTF: cgltf. Don't pull it in until the 3D spike actually starts.
ImGui: it's C++, full stop. Use dcimgui/cimgui bindings with one quarantined C++ TU in the build. Accepting that impurity is worth more than switching to Nuklear/microui and losing the best debug tooling.
Coroutines: minicoro, same as CF. Test the Emscripten path early (it runs through fiber/asyncify machinery, which affects link flags and performance) — this interacts with your web decision.
Draw/SDF: the one thing you cannot buy. Port CF's approach — it's 5.3k lines plus shaders of proven design, zlib-licensed. This is your crown-jewel module and the best use of the donor-code strategy.
Text: SDL3_ttf's GPU text engine is the boring, correct default and plugs straight into SDL_GPU. Add an MSDF atlas path later if you want stylized scalable text. Replace CF's 103k-line embedded font header with C23 #embed — a genuinely good use of the new standard.
Shaders: instead of writing your own transpiler like CF did, use SDL_shadercross offline (HLSL/SPIRV → SPIRV/DXIL/MSL). It's a heavy tool dependency (bundles DXC/SPIRV-Cross) but zero runtime weight if you precompile into your pack files.
Networking: notably absent from your list — cutting it deletes cute_net, cute_tls, cute_https, s2n, and a large pile of platform pain. Cut it.

On data-driven architecture: the load-bearing piece isn't an ECS, it's an asset database — GUID→path indirection over PhysFS mounts, offline pipeline (shader compile, atlas packing, font baking) producing one mountable pack, hot reload via mtime polling in dev builds, #embed fallbacks for built-ins. For the ECS layer, don't write your own: flecs v4 buys you reflection, prefabs, and JSON serialization (data-driven infrastructure for free) at the cost of a big dependency with its own worldview; pico_ecs, which you've already explored, is the minimal opposite. That choice can be deferred behind a clean asset layer; the asset layer can't be deferred.

C23 reality check: MSVC's C23 support remains far behind, so commit to a clang-everywhere toolchain (clang, clang-cl on Windows, AppleClang, Emscripten) — that also gets you #embed uniformly. On SIMD: Box2D and your audio lib bring their own; your hot loops are batch vertex transforms and particles. Write SoA scalar code first, verify clang's autovectorization, and hand-write SSE/NEON/WASM128 only where a profiler says so. For 2D, the GPU is rarely the thing waiting on your CPU math.

The question I'd be negligent not to raise

Your last stack review ended with a specific decision: finish the pirate encounter loop in CF as the diagnostic before reconsidering the stack. A new framework is the maximal version of reconsidering the stack, and it's also the most infrastructure-shaped project possible — squarely in the pattern you've identified in your own history. I'm not saying don't build it; a framework may legitimately be the sabbatical project, and you've been framework-curious since Bielik2D. I'm saying make that trade explicitly rather than sliding into it. The best forcing function does both at once: define v1 as "Space Delivery runs on it, feature-identical." That converts the framework from open-ended infrastructure into a bounded port with an objective finish line, and it derives your real API surface from a shipping-adjacent game instead of speculation.

How I'd run the LLM build, and what I can do next

Headers-first is the key discipline: LLMs are strong at implementing against a fixed public header and weak at incrementally designing APIs, so the .h files are where you and I spend the design effort before Claude Code writes a line. Then: one module per session with a written spec; a test per module (pico_unit-style for math/VFS/strings, golden-image tests for the renderer — render to texture, hash, compare); an example app per module as living documentation; CI on Linux + Windows-clang (+ Emscripten if you choose path (a)) from day one so generated code gets checked mechanically; and a CLAUDE.md codifying conventions — CF's own AGENTS.md is a decent template to steal the shape of. Build order: window/GPU-clear + shader pipeline → sprite batch + atlas → SDF shapes → text → input/app loop → VFS + asset DB + hot reload → audio → Box2D → coroutines → ImGui tools → the Space Delivery port.

Concrete things I can produce for you next, in roughly the order I'd do them: an audit of Space Delivery's actual cf_* call surface to define minimal v1 scope (point me at the source or paste grep -oh 'cf_[a-z_]*' src/*.c | sort -u); the architecture document with the module map and data-flow; and the full public header set for the first three milestones, spec'd for Claude Code sessions. Which do you want first?
