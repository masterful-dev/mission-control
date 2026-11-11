#pragma once
// Funkcje OpenGL 3.3 przez SDL_GL_GetProcAddress: bez bibliotek i bez linkowania z libGL.
#define GL_GLEXT_PROTOTYPES 1
#include <SDL3/SDL_opengl.h>

#define GL33_LISTA(X) \
  X(ActiveTexture) X(AttachShader) X(BindAttribLocation) X(BindBuffer) X(BindFramebuffer) X(BindRenderbuffer) X(BindTexture) X(BindVertexArray) \
  X(BlendFunc) X(BlitFramebuffer) X(BufferData) X(CheckFramebufferStatus) X(Clear) X(ClearColor) X(ColorMask) X(CompileShader) X(CreateProgram) X(CreateShader) \
  X(DeleteBuffers) X(DeleteFramebuffers) X(DeleteProgram) X(DeleteRenderbuffers) X(DeleteShader) X(DeleteTextures) X(DeleteVertexArrays) \
  X(DepthFunc) X(DepthMask) X(Disable) X(DrawArrays) X(DrawElements) X(Enable) X(EnableVertexAttribArray) X(Finish) X(FramebufferRenderbuffer) X(FramebufferTexture2D) \
  X(GenBuffers) X(GenFramebuffers) X(GenRenderbuffers) X(GenTextures) X(GenVertexArrays) X(GenerateMipmap) X(GetError) X(GetIntegerv) X(GetProgramInfoLog) X(GetProgramiv) \
  X(GetShaderInfoLog) X(GetShaderiv) X(GetString) X(GetUniformLocation) X(LinkProgram) X(PixelStorei) X(ReadBuffer) X(ReadPixels) X(RenderbufferStorage) X(RenderbufferStorageMultisample) \
  X(Scissor) X(ShaderSource) X(TexImage2D) X(TexParameteri) X(TexSubImage2D) \
  X(Uniform1f) X(Uniform1fv) X(Uniform1i) X(Uniform2f) X(Uniform2fv) X(Uniform3f) X(Uniform3fv) X(Uniform4f) X(Uniform4fv) X(UniformMatrix3fv) X(UniformMatrix4fv) \
  X(UseProgram) X(VertexAttribPointer) X(Viewport)

namespace tw {
struct Gl {
#define X(n) decltype(&::gl##n) n = nullptr;
  GL33_LISTA(X)
#undef X
};
extern Gl gl;
bool wczytajGl();   // kontekst musi byc biezacy
}
