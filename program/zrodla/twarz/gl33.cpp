#include "gl33.h"
#include <SDL3/SDL.h>

namespace tw {
Gl gl;
bool wczytajGl() {
  bool ok = true;
#define X(n) gl.n = (decltype(gl.n))SDL_GL_GetProcAddress("gl" #n); if (!gl.n) { SDL_Log("twarz: brak funkcji gl" #n); ok = false; }
  GL33_LISTA(X)
#undef X
  return ok;
}
}
