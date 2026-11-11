#version 300 es
precision highp float; precision highp int; precision highp sampler2D; precision highp usampler2D;

uniform sampler2D uPos; uniform usampler2D uOff,uNb; out vec4 o;
void main(){ivec2 q=ivec2(gl_FragCoord.xy); uvec4 a=texelFetch(uOff,q,0); vec3 p=texelFetch(uPos,q,0).xyz,n=vec3(0.);
 for(uint i=a.z;i<a.w;i++){uvec2 e=texelFetch(uNb,ivec2(int(i&2047u),int(i>>11u)),0).xy;
  vec3 A=texelFetch(uPos,ivec2(int(e.x)&511,int(e.x)>>9),0).xyz-p,B=texelFetch(uPos,ivec2(int(e.y)&511,int(e.y)>>9),0).xyz-p;n+=cross(A,B);}
 o=vec4(normalize(n+1e-9),1.);}