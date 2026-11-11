#version 300 es
precision highp float; precision highp int; precision highp sampler2D; precision highp usampler2D;

float h13(vec3 p){p=fract(p*.1031);p+=dot(p,p.zyx+31.32);return fract((p.x+p.y)*p.z);}
float vn(vec3 p){vec3 i=floor(p),f=fract(p);f=f*f*(3.-2.*f);
 return mix(mix(mix(h13(i),h13(i+vec3(1,0,0)),f.x),mix(h13(i+vec3(0,1,0)),h13(i+vec3(1,1,0)),f.x),f.y),
            mix(mix(h13(i+vec3(0,0,1)),h13(i+vec3(1,0,1)),f.x),mix(h13(i+vec3(0,1,1)),h13(i+vec3(1,1,1)),f.x),f.y),f.z);}
float fbm(vec3 p){float a=.5,s=0.;for(int i=0;i<4;i++){s+=a*vn(p);p=p*2.03+7.1;a*=.5;}return s;}
in vec2 vP; uniform vec3 uTint,uTint2; uniform float uT,uVoice,uRise; out vec4 o;
void main(){vec2 q=vP*vec2(1.,1.9);float r=length(q);
 float n=vn(vec3(q*2.4,uT*.1))*.12;
 float w=sin((r+n)*38.-uT*(1.2+uVoice*6.));float ring=pow(max(w,0.),30.)*exp(-r*1.15);
 float wide=exp(-pow(vP.y*9.,2.))*exp(-abs(vP.x)*1.1);
 float a=ring*(.10+uVoice*.3)+wide*.12+exp(-r*r*7.)*.06*uRise;
 o=vec4(mix(uTint,uTint2,.35)*a*smoothstep(2.6,1.2,r),0.);}