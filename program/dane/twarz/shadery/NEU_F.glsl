#version 300 es
precision highp float; precision highp int; precision highp sampler2D; precision highp usampler2D;
in vec2 vK; in float vY; uniform vec3 uTint,uTint2; uniform float uMat,uT,uAct,uVoice,uFloor,uRise; out vec4 o;
void main(){float sd=vK.x,top=vK.y;
 float py=mix(-1.3,.95,uMat);if(vY>py+fract(sd*13.)*.1)discard;
 float ph=fract(vY*.5-uT*(.16+sd*.22+uAct*.5)+sd*9.);
 float fire=pow(ph,16.);
 float flick=.62+.38*sin(uT*(.8+sd*2.6)+sd*40.);
 float env=smoothstep(top,top-.26,vY)*smoothstep(uFloor,uFloor+.5+fract(sd*7.)*.5,vY);
 float low=mix(.30,1.,smoothstep(-.9,-.5,vY));
 float thin=step(.45,fract(sd*17.))*.75+.25;
 float a=env*(.016+.05*uAct+.07*uVoice)*flick*low*thin;
 float f=fire*env*(.12+.5*uAct+.55*uVoice)*low;
 o=vec4(uTint*a+uTint2*f,0.);}