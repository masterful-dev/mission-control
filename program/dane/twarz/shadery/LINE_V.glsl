#version 300 es
precision highp float; precision highp int; precision highp sampler2D; precision highp usampler2D;
in vec3 aL; in vec2 aK; uniform mat4 uVP; uniform float uT; uniform vec4 uFill; out float vA; out float vF;
void main(){float kind=aK.y,u=aK.x,a=0.,f=0.,rot=0.;vec3 p=aL;
 if(kind<.5){a=.5+uFill.w*.5;rot=uT*.05;}else if(kind<1.5){a=.14;f=step(u+.004,uFill.x);}else if(kind<2.5){a=.11;f=step(u+.004,uFill.y);}else if(kind<3.5){a=(.30+.7*uFill.w)*.34;rot=-uT*.09;}else{a=uFill.w*(.05+.08*fract(u*7.3));}
 float c=cos(rot),s=sin(rot);p=vec3(p.x*c-p.z*s,p.y,p.x*s+p.z*c);vA=a;vF=f;gl_Position=uVP*vec4(p,1.);}