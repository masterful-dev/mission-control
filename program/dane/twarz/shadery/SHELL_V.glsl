#version 300 es
precision highp float; precision highp int; precision highp sampler2D; precision highp usampler2D;

uniform sampler2D uPos,uNrm,uRest,uAttr;
vec4 at(sampler2D t,int i){return texelFetch(t,ivec2(i&511,i>>9),0);}
uniform sampler2D uAttr2; uniform mat4 uVP; out vec3 vP; out vec3 vN; out vec3 vR; out vec4 vA; out vec4 vB;
void main(){int i=gl_VertexID; vec3 p=at(uPos,i).xyz; vP=p; vN=at(uNrm,i).xyz; vR=at(uRest,i).xyz; vA=at(uAttr,i); vB=at(uAttr2,i); gl_Position=uVP*vec4(p,1.);}