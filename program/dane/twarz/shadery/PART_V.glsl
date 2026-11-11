#version 300 es
precision highp float; precision highp int; precision highp sampler2D; precision highp usampler2D;

uniform sampler2D uPos,uNrm,uRest,uAttr;
vec4 at(sampler2D t,int i){return texelFetch(t,ivec2(i&511,i>>9),0);}
in vec4 aR; uniform mat4 uVP; uniform float uT,uMat,uOrbit,uSpin,uDust,uPx,uN; out float vA; out float vK;
void main(){int i=int(aR.w*uN);float k=fract(aR.x*17.3);vec3 s=at(uPos,i).xyz;vec3 p;float a=0.,size=1.;
 if(k<.6){float e=clamp(uMat*1.4-aR.x*.4,0.,1.);e=e*e*(3.-2.*e);float an=aR.y*6.283+uT*.25+(1.-e)*5.;
  vec3 cloud=vec3(cos(an),aR.z*2.-1.2,sin(an))*(1.1+aR.x*.9);
  float life=fract(aR.y*7.+uT*(.04+.08*aR.z));
  vec3 away=normalize(s+vec3(0.,.1,0.))*life*(.05+uDust*.5)+vec3(0.,life*.22,0.);
  p=mix(cloud,s+away,e);a=(1.-e)*.7*step(.002,uMat)+e*(1.-life)*life*(.12+uDust*1.3)*at(uAttr,i).w;}
 else if(k<.82){float rr=.86+aR.x*.5,sp=(.2+aR.y*.45)*(aR.z>.5?1.:-1.);float an=aR.y*20.+uSpin*sp;float ti=(aR.z-.5)*1.5;
  p=vec3(cos(an)*rr,sin(an)*rr*sin(ti)+.06,sin(an)*rr*cos(ti));a=uOrbit*(.5+.9*fract(aR.x*9.+uT*.4));size=1.5;}
 else{float an=aR.y*6.283+uT*.012;p=vec3(cos(an)*(1.3+aR.x*2.2),(aR.z*2.-1.)*1.8+sin(uT*.1+aR.x*9.)*.1,sin(an)*(1.3+aR.x*2.2)-.4);a=.13;size=.8;}
 vA=a;vK=k;gl_Position=uVP*vec4(p,1.);gl_PointSize=uPx*size*(1.8+aR.x*1.5)/gl_Position.w*3.4;}