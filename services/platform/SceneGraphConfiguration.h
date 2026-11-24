#pragma once

namespace EchoMusic
{
/// Apply image and glyph atlas defaults before Qt Quick creates any render context.
/// Explicit Qt environment overrides retain their original meaning.
void configureSceneGraph();
} // namespace EchoMusic
