
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
