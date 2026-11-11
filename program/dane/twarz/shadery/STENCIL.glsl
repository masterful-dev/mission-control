#version 300 es
precision highp float; precision highp int; precision highp sampler2D; precision highp usampler2D;

uniform sampler2D uC,uSt; uniform usampler2D uOff; uniform mat3 uHead; uniform float uT,uBreath,uGlitch; out vec4 o;
float h11(float p){p=fract(p*.1031);p*=p+33.33;p*=p+p;return fract(p);}
void main(){ivec2 q=ivec2(gl_FragCoord.xy); uvec4 a=texelFetch(uOff,q,0); vec3 p=vec3(0.);
 for(uint i=a.x;i<a.y;i++){vec2 e=texelFetch(uSt,ivec2(int(i&2047u),int(i>>11u)),0).xy;int c=int(e.x);p+=texelFetch(uC,ivec2(c&127,c>>7),0).xyz*e.y;}
 float y0=p.y;
 p.xz*=1.+uBreath*.004*smoothstep(-.55,-.95,y0);
 p.y+=uBreath*.0035*smoothstep(-1.1,-.7,y0);
 float w=smoothstep(-.96,-.52,y0);
 vec3 pv=vec3(0.,-.58,-.04);
 p=mix(p,uHead*(p-pv)+pv,w);
 if(uGlitch>.001){float sl=floor(y0*22.+floor(uT*9.)*3.1),r=h11(sl+floor(uT*17.)*7.);p.x+=step(.78,r)*(h11(sl*3.3)-.5)*uGlitch*.045;}
 o=vec4(p,1.);}