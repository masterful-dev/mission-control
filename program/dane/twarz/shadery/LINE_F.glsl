#version 300 es
precision highp float; precision highp int; precision highp sampler2D; precision highp usampler2D;
in float vA; in float vF; uniform vec3 uTint,uTint2; out vec4 o;void main(){o=vec4(mix(uTint*vA,uTint2*.34,vF),0.);}