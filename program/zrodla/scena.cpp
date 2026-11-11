// Tlo okna i podstawa sceny: jeden przebieg na caly ekran. Gradient z rozproszeniem (bez pasow), mgla, podloga w perspektywie
// i zywe widmo aktywnosci na linii podstawy. Srodek = teraz, boki = przeszlosc: slupki rosna, gdy model pracuje.
#include "scena.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <cstdio>
#include <string>
namespace {
struct { unsigned (*CreateShader)(unsigned); void (*ShaderSource)(unsigned, int, const char* const*, const int*); void (*CompileShader)(unsigned); void (*GetShaderiv)(unsigned, unsigned, int*); void (*GetShaderInfoLog)(unsigned, int, int*, char*); unsigned (*CreateProgram)(); void (*AttachShader)(unsigned, unsigned); void (*LinkProgram)(unsigned); void (*UseProgram)(unsigned); int (*GetUniformLocation)(unsigned, const char*); void (*Uniform1f)(int, float); void (*Uniform2f)(int, float, float); void (*Uniform3f)(int, float, float, float); void (*Uniform4f)(int, float, float, float, float); void (*Uniform1fv)(int, int, const float*); void (*GenVertexArrays)(int, unsigned*); void (*BindVertexArray)(unsigned); void (*DrawArrays)(unsigned, int, int); void (*Disable)(unsigned); } G;
template <class F> void wez(F& f, const char* n) { f = (F)SDL_GL_GetProcAddress(n); }
unsigned prog = 0, vao = 0; int uR, uT, uBg, uAcc, uKol, uS, uPar, uPar2, uH; Scena::Par P;
const char* VS = "#version 330 core\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0.,1.);}";
const char* FS = R"GLSL(#version 330 core
out vec4 o;
uniform vec2 uR; uniform float uT; uniform vec3 uBg, uAcc, uKol;
uniform vec4 uS;      // srodek x, y; polowa szerokosci sceny; y linii podstawy (piksele, y od dolu)
uniform vec4 uPar;    // poziom aktywnosci, blask, skala interfejsu, widmo 0/1
uniform vec4 uPar2;   // mgla, siatka, czas od klikniecia, ruch
uniform float uH[64];
float h1(float n){return fract(sin(n*127.1)*43758.5453);}
float h2(vec2 p){vec3 q=fract(vec3(p.xyx)*.1031);q+=dot(q,q.yzx+33.33);return fract((q.x+q.y)*q.z);}
float vn(vec2 p){vec2 i=floor(p),f=fract(p);f=f*f*f*(f*(f*6.-15.)+10.);return mix(mix(h2(i),h2(i+vec2(1,0)),f.x),mix(h2(i+vec2(0,1)),h2(i+vec2(1,1)),f.x),f.y);}
float fbm(vec2 p){float a=.5,s=0.;for(int i=0;i<4;i++){s+=a*vn(p);p=p*2.03+vec2(17.3,9.1);a*=.5;}return s;}
void main(){
  vec2 f=gl_FragCoord.xy; vec2 c=uS.xy; float hw=max(uS.z,1.); float by=uS.w; float sk=max(uPar.z,.5); float T=uT*uPar2.w;
  vec2 q=(f-c)/hw; float r=length(q*vec2(.75,1.));
  // tlo: lagodne rozjasnienie wokol sceny, wolna mgla, winieta
  vec3 col=mix(uBg,uBg*1.25+uAcc*.055+vec3(.008),exp(-r*r*.5));
  float m1=fbm(q*.8+vec2(T*.011,-T*.007)); float m2=fbm(q*1.9-vec2(T*.017,T*.005)+m1*1.3);
  col+=uAcc*(.07*m1*m2*uPar2.x)*smoothstep(2.4,.1,r);
  col+=uKol*(.035*uPar.x*uPar.y)*exp(-r*r*1.4);
  vec2 n=f/uR-.5; col*=1.-dot(n,n)*.6;
  float qx=q.x, ax=abs(qx), up=f.y-by, dy=-up/hw;
  // podloga: siatka w perspektywie, gasnie przy horyzoncie i na bokach
  if(dy>0.&&uPar2.y>0.&&uPar.w>0.){
    float z=.2/(dy+.015); vec2 g=vec2(qx*z*2.4,z*1.7+T*.06); vec2 w=fwidth(g); vec2 a=abs(fract(g-.5)-.5)/max(w,vec2(1e-4));
    float ln=1.-min(min(a.x,a.y),1.); ln*=1.-smoothstep(.2,.55,max(w.x,w.y)); ln*=smoothstep(0.,.05,dy)*exp(-dy*2.6)*exp(-qx*qx/3.2);
    col+=uAcc*ln*.13*uPar2.y;
    col+=uKol*exp(-dy*10.)*exp(-qx*qx/1.3)*(.05+.16*uPar.x)*uPar.y;
  }
  // linia podstawy
  float kon=smoothstep(1.3,.95,ax); float ly=abs(up);
  float lin=exp(-ly/(sk*.9))*(.3+.7*exp(-qx*qx/1.4))*kon;
  col+=mix(uKol,vec3(1.),.65)*lin*uPar.y*uPar.w;
  col+=uKol*exp(-ly/(sk*16.))*exp(-qx*qx/.7)*(.08+.22*uPar.x)*uPar.y*uPar.w;
  // widmo: waskie slupki, wysokosc z historii aktywnosci
  if(uPar.w>0.){
    float cw=max(1.,floor(sk*2.+.5)); float ci=floor((f.x-c.x)/cw); float xin=fract((f.x-c.x)/cw); float bx=abs(ci+.5)*cw/hw;
    float hp=clamp(bx/1.25,0.,1.)*63.; int i0=int(floor(hp)); float amp=mix(uH[i0],uH[min(i0+1,63)],fract(hp));
    float tt=T*8.; float tf=fract(tt); tf=tf*tf*(3.-2.*tf); float rn=mix(h2(vec2(ci,floor(tt))),h2(vec2(ci,floor(tt)+1.)),tf); float ch=.45+.55*h1(ci*.371);
    float env=exp(-bx*bx/.3)*.8+.2*exp(-bx*bx/1.4);
    float hg=hw*(.006+.34*amp*env*(.18+.82*rn*rn)*ch);
    float szp=cw>1.5?smoothstep(0.,.25,xin)*smoothstep(1.,.75,xin):1.;
    float bar=up>=0.?smoothstep(hg+sk,hg*.5,up):smoothstep(hg*.5,0.,-up)*.28;
    vec3 bc=mix(vec3(1.),uKol,clamp(ly/max(hg,1.),0.,1.)*.8+.12);
    col+=bc*bar*szp*(.5+.5*rn)*kon*uPar.y*.95;
  }
  // fala po kliknieciu w scene
  if(uPar2.z>=0.&&uPar2.z<1.6){ float rr=uPar2.z*1.5; float d=abs(length(q)-rr); col+=uKol*exp(-d*d*380.)*(1.-uPar2.z/1.6)*.5; }
  // rozproszenie: bez niego gradient na ciemnym tle uklada sie w pasy
  float d1=h2(f+fract(uT)*71.),d2=h2(f.yx+fract(uT*1.3)*37.+11.);
  col+=(d1+d2-1.)/255.;
  o=vec4(max(col,0.),1.);
}
)GLSL";
unsigned kompiluj(unsigned typ, const char* kod) { unsigned s = G.CreateShader(typ); G.ShaderSource(s, 1, &kod, nullptr); G.CompileShader(s); int ok = 0; G.GetShaderiv(s, GL_COMPILE_STATUS, &ok); if (!ok) { char b[2048]; int n = 0; G.GetShaderInfoLog(s, 2047, &n, b); b[n] = 0; fprintf(stderr, "Scena: blad shadera: %s\n", b); } return s; }
}
namespace Scena {
Par& par() { return P; }
bool init() {
  wez(G.CreateShader, "glCreateShader"); wez(G.ShaderSource, "glShaderSource"); wez(G.CompileShader, "glCompileShader"); wez(G.GetShaderiv, "glGetShaderiv"); wez(G.GetShaderInfoLog, "glGetShaderInfoLog"); wez(G.CreateProgram, "glCreateProgram"); wez(G.AttachShader, "glAttachShader"); wez(G.LinkProgram, "glLinkProgram"); wez(G.UseProgram, "glUseProgram"); wez(G.GetUniformLocation, "glGetUniformLocation");
  wez(G.Uniform1f, "glUniform1f"); wez(G.Uniform2f, "glUniform2f"); wez(G.Uniform3f, "glUniform3f"); wez(G.Uniform4f, "glUniform4f"); wez(G.Uniform1fv, "glUniform1fv"); wez(G.GenVertexArrays, "glGenVertexArrays"); wez(G.BindVertexArray, "glBindVertexArray"); wez(G.DrawArrays, "glDrawArrays"); wez(G.Disable, "glDisable");
  if (!G.CreateShader || !G.Uniform1fv) return false;
  prog = G.CreateProgram(); G.AttachShader(prog, kompiluj(GL_VERTEX_SHADER, VS)); G.AttachShader(prog, kompiluj(GL_FRAGMENT_SHADER, FS)); G.LinkProgram(prog);
  uR = G.GetUniformLocation(prog, "uR"); uT = G.GetUniformLocation(prog, "uT"); uBg = G.GetUniformLocation(prog, "uBg"); uAcc = G.GetUniformLocation(prog, "uAcc"); uKol = G.GetUniformLocation(prog, "uKol"); uS = G.GetUniformLocation(prog, "uS"); uPar = G.GetUniformLocation(prog, "uPar"); uPar2 = G.GetUniformLocation(prog, "uPar2"); uH = G.GetUniformLocation(prog, "uH");
  G.GenVertexArrays(1, &vao); return true;
}
void rysuj(int fw, int fh, double T) {
  if (!prog) return; G.Disable(GL_BLEND); G.Disable(GL_DEPTH_TEST); G.Disable(GL_SCISSOR_TEST); G.UseProgram(prog);
  G.Uniform2f(uR, (float)fw, (float)fh); G.Uniform1f(uT, (float)T); G.Uniform3f(uBg, P.tlo[0], P.tlo[1], P.tlo[2]); G.Uniform3f(uAcc, P.akcent[0], P.akcent[1], P.akcent[2]); G.Uniform3f(uKol, P.kolor[0], P.kolor[1], P.kolor[2]);
  G.Uniform4f(uS, P.cx, fh - P.cy, P.pol, fh - P.bazaY); G.Uniform4f(uPar, P.poziom, P.blask, P.skala, P.widmo ? 1.f : 0.f); G.Uniform4f(uPar2, P.mgla, P.siatka, P.puls, P.ruch); G.Uniform1fv(uH, 64, P.hist);
  G.BindVertexArray(vao); G.DrawArrays(GL_TRIANGLES, 0, 3); G.BindVertexArray(0); G.UseProgram(0);
}
}
