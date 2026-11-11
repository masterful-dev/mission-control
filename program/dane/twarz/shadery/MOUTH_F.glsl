#version 300 es
precision highp float; precision highp int; precision highp sampler2D; precision highp usampler2D;

const vec3 L1=vec3(-.600,.470,.647),L2=vec3(.880,.030,.474),L3=vec3(.263,.474,-.840),L4=vec3(0.,-.970,.243);
float ggx(vec3 N,vec3 h,float r){float a=r*r,d=max(dot(N,h),0.),q=d*d*(a*a-1.)+1.;return a*a/(3.1416*q*q);}
/* miekkie otoczenie w odbiciach: okno u gory, pasy z bokow, tafla w dole */
float env(vec3 r){float win=smoothstep(.72,.30,length((r.xy-vec2(-.38,.42))*vec2(1.,1.25)))*step(0.,r.z+.3);
 float side=smoothstep(.30,.10,abs(r.x-.86))*smoothstep(.75,.2,abs(r.y))*.34;
 float low=smoothstep(-.35,-.95,r.y)*.16;return win+side+low+.03;}

uniform vec3 uRight; uniform vec4 uStyle; uniform float uAct,uVoice;
float hh(float n,float s){return fract(sin(n*12.9898+s*78.233)*43758.5453);}
float holo(vec3 P,float lum,float T,out float tip){
 float X=dot(P,uRight)*uStyle.y,aw=fwidth(X),ci=floor(X),a=hh(ci,1.),b=hh(ci,2.);
 float pf=1.-abs(fract(X)-.5)*2.;
 float line=smoothstep(.40-aw,.40+aw,pf);line=mix(line,.55,smoothstep(.6,1.4,aw));
 float f=4.5+b*8.,sp=.16+a*.34+uAct*.30+uVoice*.2,dy=fwidth(P.y);
 float duty=mix(.14,1.04,pow(clamp(lum,0.,1.),.8));
 float ph=fract((P.y-T*sp)*f+a*37.),ew=dy*f*1.5+.02;
 float d=smoothstep(0.,ew,ph)*(1.-smoothstep(duty-ew,duty,ph));
 float f2=f*2.6,du2=duty*.8,ph2=fract((P.y-T*sp*1.7)*f2+b*53.),ew2=dy*f2*1.5+.02;
 float d2=smoothstep(0.,ew2,ph2)*(1.-smoothstep(du2-ew2,du2,ph2));
 tip=line*smoothstep(duty-.09,duty-.015,ph)*(1.-smoothstep(duty-.015,duty,ph))*step(duty,.98);
 return line*max(d,d2*.8);}

in vec3 vP; in vec3 vN; in vec3 vR; in vec4 vA; in vec4 vB;
uniform vec3 uCam,uTint,uTint2; uniform float uT,uMat,uKind; out vec4 o;
void main(){if(vR.y>mix(-1.1,.78,uMat))discard;vec3 N=normalize(vN),V=normalize(uCam-vP);
 float ao=vA.x,d1=clamp((dot(N,normalize(vec3(-.2,.45,.87)))+.1)/1.1,0.,1.);
 // im glebiej w ustach, tym ciemniej
 float deep=smoothstep(.30,.545,vR.z);
 vec3 c;
 if(uKind<.5)c=uTint*.05*deep*deep*ao;
 else if(uKind<1.5)c=mix(uTint,uTint2,.25)*(.04+.26*d1*d1)*deep*deep*ao;
 else{float s=pow(max(dot(N,normalize(V+L1)),0.),60.);c=mix(uTint,uTint2,.8)*(.08+.50*d1*d1)*deep*deep*mix(.35,1.,ao)+uTint2*s*.25*deep;}
 float tip,m=holo(vP,dot(c,vec3(.3,.5,.2))*2.,uT,tip);
 o=vec4(c*(.3+m*1.1),1.);}