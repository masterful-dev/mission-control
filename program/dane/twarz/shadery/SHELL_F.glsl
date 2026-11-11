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
uniform vec3 uCam,uTint,uTint2; uniform float uT,uMat,uSweep,uFlick,uThink,uBrows; uniform sampler2D uPore;
out vec4 o;
void main(){
 float ao=vA.x,lip=vA.y,lico=vA.z,fade=vA.w,cav=vB.x,thick=vB.y,brow=vB.z,liner=vB.w;
 if(fade<.004) discard;
 vec3 N=normalize(vN),V=normalize(uCam-vP);
 // drobna faktura: dwa rzuty tej samej mapy; na ustach pionowe bruzdki
 vec2 g=mix(texture(uPore,vR.xy*6.1).xy,texture(uPore,vR.zy*6.1).xy,smoothstep(.4,.7,abs(N.x)))-.5;
 float stri=sin(vR.x*640.+sin(vR.y*90.)*1.7);
 vec3 Ns=normalize(N+vec3(g*(.62*(1.-lip))+vec2(stri*.055*lip,0.),0.));
 // swiatlo: ono decyduje, jak dlugie sa kreski w danym miejscu
 float nv=max(dot(N,V),0.),fres=pow(max(1.-nv,0.),4.);
 float d1=clamp((dot(N,L1)+.16)/1.16,0.,1.);d1=d1*d1*(3.-2.*d1);
 float d2=clamp((dot(N,L2)+.1)/1.1,0.,1.);
 float d4=clamp((dot(N,L4)+.35)/1.35,0.,1.);
 float occ=mix(1.,ao*sqrt(ao),.80);
 float diff=(d1*.78+d2*d2*.16+d4*d4*(.06+uVoice*.08)+.05*(.7+.3*N.y))*occ;
 diff*=mix(.72,1.14,cav);
 diff*=1.-.42*brow*uBrows;
 diff*=1.-.52*liner*uBrows;
 diff=mix(diff,diff*.62+.05,lip);
 vec3 h1=normalize(V+L1),h2=normalize(V+L2);
 float F=.028+.972*pow(max(1.-max(dot(V,h1),0.),0.),5.),rough=mix(.40,.30,lip);
 float spec=(ggx(Ns,h1,rough)*.62+ggx(Ns,h1,rough+.24)*.38)*F*max(dot(N,L1),0.)*2.6+ggx(Ns,h2,.46)*F*.42*max(dot(N,L2),0.);
 spec+=env(reflect(-V,Ns))*(.022+.5*fres)*mix(.75,1.15,cav);
 spec*=occ*mix(.45,1.1,cav)*mix(1.,1.45,lip);
 float rim=fres*(.05+.85*max(dot(N,L3),0.))*occ;
 float lum=(diff+spec*.8+rim*.6)*mix(.42,1.,smoothstep(.60,.16,vR.y));
 // kreski
 float tip,m=holo(vP,lum*.96,uT,tip);
 float K=uStyle.x;
 vec3 c1=mix(uTint,uTint2,.35+.6*clamp(lum,0.,1.));
 vec3 col=c1*(.14+lum)*m*1.02*K+uTint2*tip*(.25+uStyle.z*.8)*(.3+lum);
 // slaba poswiata miedzy kreskami trzyma ksztalt twarzy
 col+=mix(uTint,uTint2,.45)*lum*(.20+.22*uStyle.w);
 float sweep=exp(-pow((vR.y-uSweep)/.018,2.));
 col+=uTint2*sweep*.06*(.15+d1);
 float hc=hh(floor(dot(vP,uRight)*uStyle.y),3.);
 float alpha=mix(.06,.94,lico);
 // kondensacja z kresek od dolu
 float py=mix(-1.1,.78,uMat);
 float dd=py-vR.y+hc*.16;
 if(dd<0.) discard;
 col+=uTint2*exp(-dd*20.)*1.1*(uMat<.99?1.:0.);
 alpha*=smoothstep(0.,.12,dd);
 col*=1.-uFlick*(.5+.5*sin(uT*91.+vR.y*40.))*.5;
 float fz=smoothstep(hc*.65,hc*.65+.3,fade);
 col*=fz*mix(.35,1.,lico);alpha*=fz*fz;
 o=vec4(col,alpha);
}