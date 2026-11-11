#version 300 es
precision highp float; precision highp int; precision highp sampler2D; precision highp usampler2D;
in vec3 vP; in vec3 vN; in vec3 vR; in vec4 vA; in vec4 vB; uniform float uMat; out vec4 o;
void main(){if(vR.y>mix(-1.1,.78,uMat))discard;float t=vA.x,up=vA.y;float a=pow(1.-t,1.7)*mix(.62,.96,up);a=max(a,up*.34*smoothstep(1.,.2,t));o=vec4(0.,0.,0.,a);}