#version 300 es
precision highp float; precision highp int; precision highp sampler2D; precision highp usampler2D;
in vec3 aP; uniform mat4 uVP; uniform mat3 uRot; uniform vec3 uC; uniform float uR; out vec3 vL; out vec3 vW; out vec3 vN;
void main(){vL=aP;float b=smoothstep(.70,.97,aP.z);vec3 q=aP*(1.+.125*b);vec3 p=uC+uRot*q*uR;vW=p;
 vec3 nn=normalize(aP+vec3(0.,0.,0.)-vec3(aP.xy*b*.0,0.));vN=uRot*normalize(mix(aP,normalize(aP-vec3(0.,0.,.42)),b));gl_Position=uVP*vec4(p,1.);}