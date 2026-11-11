#version 300 es
precision highp float; precision highp int; precision highp sampler2D; precision highp usampler2D;

float h13(vec3 p){p=fract(p*.1031);p+=dot(p,p.zyx+31.32);return fract((p.x+p.y)*p.z);}
float vn(vec3 p){vec3 i=floor(p),f=fract(p);f=f*f*(3.-2.*f);
 return mix(mix(mix(h13(i),h13(i+vec3(1,0,0)),f.x),mix(h13(i+vec3(0,1,0)),h13(i+vec3(1,1,0)),f.x),f.y),
            mix(mix(h13(i+vec3(0,0,1)),h13(i+vec3(1,0,1)),f.x),mix(h13(i+vec3(0,1,1)),h13(i+vec3(1,1,1)),f.x),f.y),f.z);}
float fbm(vec3 p){float a=.5,s=0.;for(int i=0;i<4;i++){s+=a*vn(p);p=p*2.03+7.1;a*=.5;}return s;}

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

in vec3 vL; in vec3 vW; in vec3 vN; uniform vec3 uCam,uEye,uTint,uTint2; uniform mat3 uRot; uniform float uT,uPupil,uThink,uMat,uIrE;
out vec4 o;
void main(){
 vec3 n=normalize(vL),V=normalize(uCam-vW),Nw=normalize(vN);
 vec3 Vl=transpose(uRot)*V;
 // zalamanie: teczowka lezy glebiej niz rogowka
 vec2 c=n.xy-Vl.xy*.14*smoothstep(.62,1.,n.z);
 float r=length(c),ang=atan(c.y,c.x);
 float R=.405,pr=.128*(1.+uPupil*.55);
 float irisM=smoothstep(R+.022,R-.018,r)*step(0.,n.z);
 float t=clamp((r-pr)/(R-pr),0.,1.);
 vec3 dir=vec3(cos(ang),sin(ang),0.);
 // teczowka: promieniste wlokna, kolnierz, krypty
 float f1=fbm(vec3(dir.xy*7.,t*2.2)),f2=fbm(vec3(dir.xy*19.,t*1.2+3.)),f3=vn(vec3(dir.xy*44.,t*2.+7.));
 float fib=f1*.55+f2*.45+f3*.22;
 float crypt=smoothstep(.40,.66,fbm(vec3(dir.xy*5.,t*5.+1.)));
 float collar=exp(-pow((t-.26)/.09,2.));
 vec3 ic=uEye*(.10+.74*fib*(.45+.75*t));
 ic=mix(ic,mix(uEye,uTint2,.55)*(.5+.6*fib),collar*.55);
 ic*=1.-crypt*.30*smoothstep(.1,.5,t);
 // swiatlo wpada przez rogowke: jasniej po stronie przeciwnej do lampy
 float caust=smoothstep(-.1,.9,dot(normalize(c+1e-4),-normalize(L1.xy)))*smoothstep(.2,.9,t);
 ic*=.62+.85*caust;
 // cienka obwodka pracy modelu w teczowce
 float tick=pow(.5+.5*sin(ang*36.),10.);
 float ring=exp(-pow((r-R*.80)/.0042,2.))*(.2+.5*tick)*(.55+.45*sin(ang*3.+uT*(.5+uThink*4.)));
 ic+=uTint2*ring*(.10+uThink*.8);
 ic=ic*(1.+uIrE*1.5)+uEye*uIrE*(.04+.34*fib)*smoothstep(.05,.8,t);
 float limb=smoothstep(R-.11,R+.016,r);
 ic*=1.-limb*.97;
 float pup=smoothstep(pr+.014,pr-.014,r);
 vec3 col=ic*(1.-pup);
 // twardowka: jasna, chlodna, ciemnieje ku katom; zylki bardzo slabe
 float d1=clamp((dot(Nw,L1)+.45)/1.45,0.,1.);
 float vein=smoothstep(.56,.74,fbm(n*9.))*smoothstep(.3,.9,1.-n.z)*.20;
 vec3 sc=mix(uTint,uTint2,.72)*(.15+.25*d1)*(.55+.45*smoothstep(.15,.85,n.z))*(1.-vein);
 sc*=mix(1.,.72,exp(-pow((r-R-.05)/.06,2.)));
 col=mix(sc,col,irisM);
 // rogowka: odbicie okna, drugie swiatlo, polysk
 vec3 Rf=reflect(-V,Nw);float fr=.03+.97*pow(max(1.-max(dot(Nw,V),0.),0.),5.);
 float win=smoothstep(.15,.02,abs(Rf.x+.36))*smoothstep(.20,.04,abs(Rf.y-.44));
 win*=1.-.75*smoothstep(.012,.0,abs(Rf.x+.36))-.0;
 float small=smoothstep(.08,.015,length(Rf.xy-vec2(.44,.10)));
 float wetspec=ggx(Nw,normalize(V+L1),.16)*.018;
 col+=uTint2*(win*1.15+small*.28+wetspec)+uTint2*env(Rf)*fr*.5;
 float py=mix(-1.25,.78,uMat);
 if(py<vW.y) discard;
 // oko tez jest z kresek, ale swieci mocniej niz skora
 float l=dot(col,vec3(.3,.5,.2)),tip,m=holo(vW,l*1.5+.2,uT,tip);
 col=col*(.8+m*.6)+uTint2*tip*.2;
 o=vec4(col,1.);
}