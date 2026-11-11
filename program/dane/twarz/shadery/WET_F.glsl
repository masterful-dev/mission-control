#version 300 es
precision highp float; precision highp int; precision highp sampler2D; precision highp usampler2D;

const vec3 L1=vec3(-.600,.470,.647),L2=vec3(.880,.030,.474),L3=vec3(.263,.474,-.840),L4=vec3(0.,-.970,.243);
float ggx(vec3 N,vec3 h,float r){float a=r*r,d=max(dot(N,h),0.),q=d*d*(a*a-1.)+1.;return a*a/(3.1416*q*q);}
/* miekkie otoczenie w odbiciach: okno u gory, pasy z bokow, tafla w dole */
float env(vec3 r){float win=smoothstep(.72,.30,length((r.xy-vec2(-.38,.42))*vec2(1.,1.25)))*step(0.,r.z+.3);
 float side=smoothstep(.30,.10,abs(r.x-.86))*smoothstep(.75,.2,abs(r.y))*.34;
 float low=smoothstep(-.35,-.95,r.y)*.16;return win+side+low+.03;}
in vec3 vP; in vec3 vN; in vec3 vR; in vec4 vA; in vec4 vB; uniform vec3 uCam,uTint,uTint2; uniform float uMat; out vec4 o;
void main(){if(vR.y>mix(-1.1,.78,uMat))discard;vec3 N=normalize(vN),V=normalize(uCam-vP);float s=ggx(N,normalize(V+L1),.18)*.05;
 float f=pow(max(1.-max(dot(N,V),0.),0.),3.);vec3 c=mix(uTint,uTint2,.8)*(.05+.16*f)+uTint2*min(s,1.2);o=vec4(c,.32);}