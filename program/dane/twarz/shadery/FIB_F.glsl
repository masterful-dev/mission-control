#version 300 es
precision highp float; precision highp int; precision highp sampler2D; precision highp usampler2D;
in float vA; in float vU; in float vK; in float vL; uniform vec3 uTint,uTint2; uniform float uK,uMat,uBrows; out vec4 o;void main(){if(uMat<.8||(vK<1.5&&uBrows<.5))discard;float g=(uMat-.8)*5.;
 if(vK>=5.){o=vec4(mix(uTint,uTint2,.55)*vL*g,0.);return;}
 float a=vA*g*(vK<1.5?.40:.88);o=vec4(uTint*.03*a,a);}