#version 300 es
precision highp float; precision highp int; precision highp sampler2D; precision highp usampler2D;
in vec2 v; uniform sampler2D s,b; uniform vec3 uTint,uTint2; uniform float uGlow,uScan,uGlitch,uT,uPx,uAsp,uHz,uRoom; out vec4 o;
float h(vec2 p){return fract(sin(dot(p,vec2(12.9898,78.233)))*43758.5453);}
vec3 aces(vec3 x){return clamp(x*(2.51*x+.03)/(x*(2.43*x+.59)+.14),0.,1.);}
/* pomieszczenie: luk okna, cienkie poziome pasy, horyzont tafli */
vec3 room(vec2 q){vec2 p=(q-vec2(.5,uHz))*vec2(uAsp,1.);float x=p.x,y=p.y,c=exp(-x*x*.9);
 float e=length(p*vec2(.62,1.)-vec2(0.,.08));
 float arc=exp(-pow((e-.86)/.0016,2.))*.5+exp(-pow((e-.90)/.0011,2.))*.22;
 float glass=smoothstep(.87,.5,e)*.016;
 float bl=0.;for(int i=0;i<5;i++){float f=float(i),yy=.10+f*.135+f*f*.012,w=.0008+.0005*f,d=(y-yy)/w;if(abs(d)<3.){float n=sin(x*5.+f*2.1)+sin(x*2.3+f*5.+uT*.05);bl+=exp(-d*d)*smoothstep(-.6,.9,n)*(.22-.03*f);}}
 float hz=exp(-pow(y/.0016,2.))*.5*exp(-x*x*.5),haze=exp(-pow(y/.13,2.))*exp(-x*x*.55)*.045,up=step(0.,y);
 return uTint*((arc+bl)*up*c*.28+glass*up+haze)+uTint2*hz*.5;}
void main(){vec2 cc=v-.5;float ab=uGlitch*.006+.0009*dot(cc,cc)*4.;
 float sh=uGlitch>.01?(step(.93,h(vec2(floor(v.y*40.),floor(uT*12.))))*uGlitch*.03):0.;vec2 q=v+vec2(sh,0.);
 vec4 s0=texture(s,q);
 vec3 c=uGlitch>.01?vec3(texture(s,q+cc*ab*2.).r,s0.g,texture(s,q-cc*ab*2.).b):s0.rgb;
 c+=texture(b,q).rgb*uGlow+room(v)*uRoom;
 float sc=sin(gl_FragCoord.y*3.14159/uPx/1.5);c*=1.-uScan*.16*sc*sc;
 c+=c*(h(v+uT)-.5)*.05;
 c=aces(c*1.15);
 float a=clamp(max(c.r,max(c.g,c.b))*1.4,0.,1.);o=vec4(c,a);}