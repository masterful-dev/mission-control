#version 300 es
precision highp float; precision highp int; precision highp sampler2D; precision highp usampler2D;
in vec4 aS; in float aU; uniform mat4 uVP; uniform float uT,uFloor,uRise,uMir,uBands[32],uVoice,uAct; out float vU; out float vW;
void main(){float x=aS.x,z=aS.y,sd=aS.z,w=aS.w;
 float b=clamp(abs(x)*1.2,0.,1.)*31.;int i=int(b);float f=fract(b);
 float band=mix(uBands[i],uBands[min(i+1,31)],f);
 float env=exp(-x*x*5.2);
 float grain=pow(fract(sd*7.3),2.2);
 float wob=.5+.5*sin(uT*(.5+sd*1.6)+sd*50.);
 float hh=.012+env*(.08+grain*.62+.12*wob+band*(.5+grain*.9))*uRise;
 vec3 p=vec3(x,uFloor+aU*hh*(uMir>.5?-.55:1.),z);
 vU=aU;vW=w*(.3+.7*fract(sd*3.1))*smoothstep(1.5,.3,abs(x))*(uMir>.5?.13:1.);
 gl_Position=uVP*vec4(p,1.);}