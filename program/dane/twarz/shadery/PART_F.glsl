#version 300 es
precision highp float; precision highp int; precision highp sampler2D; precision highp usampler2D;
in float vA; in float vK; uniform vec3 uTint,uTint2; out vec4 o;void main(){float d=length(gl_PointCoord-.5);float m=exp(-d*d*16.);o=vec4(mix(uTint,uTint2,.3)*m*vA,0.);}