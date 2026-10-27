# 0646. Draw pictures through a texture facade over ImGui's user textures

Status: proposed
Date: 2026-10-25

## Context

Graphite is about to show image files. Until now the font atlas was the only
texture, and nothing called `ImGui::Image` or `AddImage`. The vendored ImGui
(1.93 WIP) has `RendererHasTextures`, and the OpenGL3 backend sets it. The
backend creates, updates and destroys every `ImTextureData` listed in
`platform_io.Textures` while it renders. Code outside ImGui can add its own
entries to that list with `RegisterUserTexture`, which ImGui marks
experimental.

The views that draw pictures are tested headless, with no renderer behind them.
A picture's pixels must stay sharp squares from 100% up and be sampled smoothly
below that, so sampling changes from one draw to the next. The backend has
callbacks that switch the sampler, `DrawCallback_SetSamplerNearest` and
`DrawCallback_SetSamplerLinear`.

## Decision

- **An interface in Core.** `Core/ImGui/ImageTexture.h` declares `ImageTexture`
  (`GetRef`, `GetSize`), `TextureFactory` (`Create` from an `RgbaImage`,
  `GetMaximumSide`) and `DrawTexture(drawList, texture, where, sampling)`. A
  view takes a `TextureFactory&` and draws through `DrawTexture`. Tests pass a
  fake.
- **One user texture per picture.** `ImGuiTextureFactory` copies the image into
  an RGBA32 `ImTextureData` and registers it with `RegisterUserTexture`. The
  backend uploads it the first time it renders a frame that lists it.
- **Release marks, it does not destroy.** Dropping a handle sets the data's
  `WantDestroyNextFrame` and moves the data to a retired list. When the next
  frame begins, ImGui turns the mark into `WantDestroy`. The backend then frees
  the GPU texture and sets `Destroyed`. `CollectRetired` runs at the end of
  every `ImGuiRenderer::EndFrame`. It unregisters and frees each entry that is
  `Destroyed`, and takes it out of `platform_io.Textures` too.
- **The renderer owns the factory.** `ImGuiRenderer::Init` reads
  `GL_MAX_TEXTURE_SIZE` once and hands it to the factory as
  `GetMaximumSide`. `ImGuiRenderer::GetTextures()` exposes the factory. The
  renderer destroys it after the backend has shut down, which frees what is
  still on the GPU, and before the ImGui context.
- **Sampling is chosen per draw.** A `Nearest` draw puts the backend's
  nearest-sampler callback before its `AddImage` and the linear one after it. A
  backend that lacks either callback gets a smooth draw.

## Rationale

Registering with ImGui leaves the GPU work to the backend, which already
handles the font atlas's textures this way, so Core makes no GL call beyond
reading the limit. Marking a texture rather than asking for its destruction at
once is how ImGui retires its own atlas textures. The frame being built may
still draw the texture, and without the mark ImGui reads the backend's
`Destroyed` as a lost texture and creates it again. A sampler switch around one
draw costs nothing at rest, where a second texture per filter would double the
upload.

## Consequences

### Positive

- Views that draw pictures run in headless tests with a fake factory.
- Core holds no texture upload code; pictures take the same path through the
  backend as the font atlas.
- A picture is uploaded once, whatever its zoom.

### Negative

- `RegisterUserTexture` is experimental, and `WantDestroyNextFrame` is marked
  internal. An ImGui update may change either, and the factory's tests are
  where that would show.
- A texture keeps its CPU copy of the pixels while it lives, because the backend
  recreates a texture it lost from that copy. A picture's pixels are therefore
  held twice in memory and once on the GPU.
- A released texture stays on the GPU until the frame after its release has
  been rendered.

### Neutral

- Sampling belongs to the draw, not the texture: the same texture can be drawn
  nearest in one place and smooth in another in one frame.

## Alternatives Considered

- **Two textures per picture, one per filter.** No sampler switch, but twice the
  uploads and GPU memory, for a choice that changes only as the zoom crosses
  100%.
- **GL textures made in Core and passed as `ImTextureID`.** Ties more of Core to
  OpenGL. It repeats the backend's upload and state-saving code. ImGui would
  not see these textures, so nothing would free them when the backend shuts
  down.
- **The filter set with `glTexParameter` on the texture.** Needs GL in the view,
  and the backend binds its own sampler objects, which override a texture's
  parameters.

## Related

- [0002](0002-keep-core-free-of-ide-knowledge.md)
- [0645](0645-add-stb-as-a-dependency-for-decoding-and-scaling-images.md)
