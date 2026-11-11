#version 300 es
precision highp float; precision highp int; precision highp sampler2D; precision highp usampler2D;

uniform sampler2D uPos,uNrm,uRest,uAttr;
vec4 at(sampler2D t,int i){return texelFetch(t,ivec2(i&511,i>>9),0);}

const vec3 L1=vec3(-.600,.470,.647),L2=vec3(.880,.030,.474),L3=vec3(.263,.474,-.840),L4=vec3(0.,-.970,.243);
float ggx(vec3 N,vec3 h,float r){float a=r*r,d=max(dot(N,h),0.),q=d*d*(a*a-1.)+1.;return a*a/(3.1416*q*q);}
/* miekkie otoczenie w odbiciach: okno u gory, pasy z bokow, tafla w dole */
float env(vec3 r){float win=smoothstep(.72,.30,length((r.xy-vec2(-.38,.42))*vec2(1.,1.25)))*step(0.,r.z+.3);
 float side=smoothstep(.30,.10,abs(r.x-.86))*smoothstep(.75,.2,abs(r.y))*.34;
 float low=smoothstep(-.35,-.95,r.y)*.16;return win+side+low+.03;}
in float aRoot; in vec3 aOff; in vec2 aK; uniform mat4 uVP; uniform mat3 uHead; uniform float uBlinkL,uBlinkR,uT,uAct,uVoice; out float vA; out float vU; out float vK; out float vL;
void main(){int i=int(aRoot);vec3 p=at(uPos,i).xyz;vec3 of=aOff;float u=aK.x,kind=aK.y;
 if(kind>1.5&&kind<3.5){float a=(kind<2.5?uBlinkL:uBlinkR)*1.5,c=cos(a),s=sin(a);of=vec3(of.x,of.y*c-of.z*s,of.y*s+of.z*c);}
 vec3 w=p+uHead*of;
 vA=(1.-u*u*.75)*at(uAttr,i).w;vU=u;vK=kind;vL=0.;
 if(kind>=5.){float sd=fract(kind);vec3 n=normalize(w-vec3(0.,.14,-.06));float d=clamp(dot(n,L1)*.75+.35,0.,1.),rim=pow(max(1.-max(n.z,0.),0.),2.);
  float ph=pow(fract(u*.7-uT*(.07+sd*.1+uAct*.3)+sd*9.),14.);
  vL=(.05+.42*d*d+.12*rim)*(.45+.55*fract(sd*7.3))*(1.-u*u*.82)+ph*(.10+.34*uAct+.4*uVoice)*(1.-u*.5);}
 gl_Position=uVP*vec4(w,1.);}