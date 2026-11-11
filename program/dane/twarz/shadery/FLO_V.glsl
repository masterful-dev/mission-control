#version 300 es
precision highp float; precision highp int; precision highp sampler2D; precision highp usampler2D;
in vec2 aP; uniform mat4 uVP; uniform float uFloor; out vec2 vP; void main(){vP=aP;gl_Position=uVP*vec4(aP.x,uFloor,aP.y,1.);}