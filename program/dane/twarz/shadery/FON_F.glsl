#version 300 es
precision highp float; precision highp int; precision highp sampler2D; precision highp usampler2D;
in float vU; in float vW; uniform vec3 uTint,uTint2; out vec4 o;void main(){float t=1.-vU;float a=(.02+.42*t*t*t+.06*smoothstep(.8,1.,vU))*vW;o=vec4(mix(uTint,uTint2,t*t*.8)*a,0.);}