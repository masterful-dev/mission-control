#version 300 es
precision highp float; precision highp int; precision highp sampler2D; precision highp usampler2D;
out vec2 v;void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);v=p;gl_Position=vec4(p*2.-1.,0.,1.);}